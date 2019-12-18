#include "FreeRTOS.h"
#include "board_io.h"
#include "char_map.h"
#include "common_macros.h"
#include "ff.h"
#include "gpio.h"
#include "gpio_int.h"
#include "mp3_driver.h"
#include "queue.h"
#include "sj2_cli.h"
#include "sl_string.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
//#include "ssp2_lab.h"
#include "PCD8544_SPI2.h"
#include <stdbool.h>

#define MAX_VOLUME 0x36
#define reset_volume 0x7070
#define data_size 1024
int current_song_num = 0;
int currentIndex;

QueueHandle_t Song_Q, Data_Q, button2_Q;
QueueHandle_t scroll_direction;
QueueHandle_t volume_direction;
SemaphoreHandle_t Pause_Signal;
SemaphoreHandle_t Button2_Signal;
SemaphoreHandle_t Decoder_lock;

gpio_s data_request;
bool change_song;
void read_song_name_task(void *params);
void send_data_to_decode(void *params);
void read_pause_task(void *params);
void volume_control_task(void *params);
void read_button2_task(void *params);
void lcd_controller_task(void *params);
void process_button2(void *params);

void send_pause_signal_isr(void) {
  LPC_GPIOINT->IO2IntClr |= (1 << 0);
  xSemaphoreGiveFromISR(Pause_Signal, NULL);
};

void send_button2_signal_isr(void) {
  LPC_GPIOINT->IO0IntClr |= (1 << 6);
  xSemaphoreGiveFromISR(Button2_Signal, NULL);
};

void volume_change_isr(void) {
  LPC_GPIOINT->IO2IntClr |= (1 << 1);
  bool A = LPC_GPIO2->PIN & (1 << 1);
  bool B = LPC_GPIO2->PIN & (1 << 2);

  bool direction = (A != B);
  xQueueSendFromISR(volume_direction, &direction, NULL);
}

void scroll_change_isr(void) {
  LPC_GPIOINT->IO0IntClr |= (1 << 7);
  bool E = LPC_GPIO0->PIN & (1 << 7);
  bool F = LPC_GPIO0->PIN & (1 << 8);

  bool direction = (E != F);
  xQueueSendFromISR(scroll_direction, &direction, NULL);
}

gpio_s A, B;

int main(void) {

  Song_Q = xQueueCreate(1, sizeof(name));
  Data_Q = xQueueCreate(3, sizeof(char[data_size]));
  volume_direction = xQueueCreate(1, sizeof(bool));
  scroll_direction = xQueueCreate(1, sizeof(bool));
  button2_Q = xQueueCreate(1, sizeof(uint8_t));
  change_song = false;

  playback_status_init();
  // sj2_cli__init();

  Pause_Signal = xSemaphoreCreateBinary();
  Button2_Signal = xSemaphoreCreateBinary();
  Decoder_lock = xSemaphoreCreateMutex();

  gpio_s pause_button = gpio__construct_as_input(GPIO__PORT_2, 0);
  data_request = gpio__construct_as_input(GPIO__PORT_2, 6);
  gpio_s button2 = gpio__construct_as_input(GPIO__PORT_0, 6);

  gpio_s E, F;
  E = gpio__construct_as_input(GPIO__PORT_0, 7); // for next button  = third pin
  F = gpio__construct_as_input(GPIO__PORT_0, 8); // for next button = middle pin

  A = gpio__construct_as_input(GPIO__PORT_2, 1); // for pause button = third pin
  B = gpio__construct_as_input(GPIO__PORT_2, 2); // for pause button = middle pin

  gpio2__attach_interrupt(1, GPIO_INTR__RISING_EDGE, volume_change_isr);

  gpio0__attach_interrupt(7, GPIO_INTR__RISING_EDGE, scroll_change_isr);

  gpio2__attach_interrupt(0, GPIO_INTR__RISING_EDGE, send_pause_signal_isr);
  gpio0__attach_interrupt(6, GPIO_INTR__RISING_EDGE, send_button2_signal_isr);

  FATFS fs;
  FRESULT res;
  char buff[256];

  xTaskCreate(read_song_name_task, "Read_Song", data_size + 512, NULL, 2, NULL);
  xTaskCreate(send_data_to_decode, "Send_Song", data_size + 512, NULL, 2, NULL);
  xTaskCreate(read_pause_task, "Pause Task", 512, NULL, 3, NULL);
  xTaskCreate(volume_control_task, "Volume Task", 512, NULL, 3, NULL);

  xTaskCreate(read_button2_task, "Button 2 Task", 512, NULL, 3, NULL);
  xTaskCreate(process_button2, "Prev and Next Task", 512, NULL, 3, NULL);
  xTaskCreate(lcd_controller_task, "LCD Controller Task", 1024, NULL, 3, NULL);
  vTaskStartScheduler(); // This function never returns unless RTOS scheduler runs out of memory and fails

  return 1;
}

char tracklist[10][25] = {"Centuries.mp3", "Chase Me.mp3", "Copycat.mp3", "example.mp3",          "First Love.mp3",
                          "test.mp3",      "test2.mp3",    "test3.mp3",   "Thnks fr th Mmrs.mp3", "You and I.mp3"};

void lcd_controller_task(void *p) {
  lcd_setup_normal();
  fprintf(stderr, "Setup should be complete\n");
  bool backwards = false;

  currentIndex = 0;
  lcd_print_bank(0, " ");
  lcd_print_bank(1, tracklist[currentIndex]);
  lcd_print_bank(2, tracklist[currentIndex + 1]);
  lcd_print_bank(3, tracklist[currentIndex + 2]);
  lcd_print_bank(4, "--------------");
  lcd_print_status_bank(false, 71);

  bool scroll_direction_from_queue;
  while (1) {
    if (xQueueReceive(scroll_direction, &scroll_direction_from_queue, portMAX_DELAY)) {

      if (scroll_direction_from_queue) {
        if (currentIndex != 10) {
          currentIndex++;
        }
      } else {
        if (currentIndex != 0) {
          currentIndex--;
        }
      }

      if (xSemaphoreTake(Decoder_lock, portMAX_DELAY)) {
        // print the row above highlighted row
        if (currentIndex == 0) {
          lcd_print_bank(0, " ");
        } else {
          lcd_print_bank(0, tracklist[currentIndex - 1]);
        }
        xSemaphoreGive(Decoder_lock);
      }
      if (xSemaphoreTake(Decoder_lock, portMAX_DELAY)) {
        // print current row
        lcd_print_bank(1, tracklist[currentIndex]);
        xSemaphoreGive(Decoder_lock);
      }
      // print subsequent rows
      if (xSemaphoreTake(Decoder_lock, portMAX_DELAY)) {
        if (tracklist[currentIndex + 1]) {
          lcd_print_bank(2, tracklist[currentIndex + 1]);
        }
        xSemaphoreGive(Decoder_lock);
      }
      if (xSemaphoreTake(Decoder_lock, portMAX_DELAY)) {
        if (tracklist[currentIndex + 2]) {
          lcd_print_bank(3, tracklist[currentIndex + 2]);
        }
        xSemaphoreGive(Decoder_lock);
      }
      if (xSemaphoreTake(Decoder_lock, portMAX_DELAY)) {
        lcd_print_bank(4, "--------------");
        xSemaphoreGive(Decoder_lock);
      }
    }
  }
}

void read_song_name_task(void *params) {

  char name[32];
  FIL mp3file;
  UINT bytes_read, total_read;
  FRESULT result;
  UINT file_size;

  char bytes_to_send[data_size];
  const uint8_t next_song_state = 2;
  while (1) {

    xQueueReceive(Song_Q, &name, portMAX_DELAY);

    playback__set_playing();

    xSemaphoreTake(Decoder_lock, portMAX_DELAY);
    lcd_update_playstatus(true);
    xSemaphoreGive(Decoder_lock);
    result = f_open(&mp3file, name, FA_READ | FA_OPEN_EXISTING);

    if (result == FR_OK) { // file does exist!
      file_size = f_size(&mp3file);
      total_read = 0;

      while (file_size > total_read) {

        while (playback__is_paused()) {
          vTaskDelay(1);
        }

        if (change_song) {
          break;
        }

        f_read(&mp3file, &bytes_to_send, data_size, &bytes_read);
        total_read += bytes_read;

        xQueueSend(Data_Q, bytes_to_send, portMAX_DELAY);
      }

      f_close(&mp3file);
      playback__clear_playing();

      if (change_song == false) {
        xQueueSend(button2_Q, &next_song_state, portMAX_DELAY);
      } else {
        change_song = false;
      }
    }

    vTaskDelay(1000);
  }
}

void send_data_to_decode(void *params) {

  char bytes[data_size];

  ssp0__init(1);
  decoder__init(reset_volume);
  ssp0__init(4);

  int index;
  while (1) {
    while (playback__is_paused()) {
      vTaskDelay(1);
    }

    xQueueReceive(Data_Q, bytes, portMAX_DELAY);

    for (int i = 0; i < data_size / 32; ++i) {
      while (!gpio__get(data_request)) {
        vTaskDelay(1);
      }

      for (int j = 0; j < 32; ++j) {
        index = i * 32 + j;
        xSemaphoreTake(Decoder_lock, portMAX_DELAY);
        decoder__write_data(bytes[index]);
        xSemaphoreGive(Decoder_lock);
      }
    }
  }
}

void read_pause_task(void *params) {
  while (1) {
    xSemaphoreTake(Pause_Signal, portMAX_DELAY);

    if (playback__is_playing()) {
      playback__toggle_pause();
      xSemaphoreTake(Decoder_lock, portMAX_DELAY);
      if (!playback__is_paused() && playback__is_playing()) {
        lcd_update_playstatus(true);
      } else {
        lcd_update_playstatus(false);
      }

      xSemaphoreGive(Decoder_lock);
    }
  }
}

void read_button2_task(void *params) {
  uint8_t state;
  while (1) {

    state = 0;
    if (xSemaphoreTake(Button2_Signal, portMAX_DELAY)) {
      state = 1;
      if (xSemaphoreTake(Button2_Signal, 200)) {
        state = 2;
        if (xSemaphoreTake(Button2_Signal, 200)) {
          state = 3;
        }
      }
    }
    xQueueSend(button2_Q, &state, portMAX_DELAY);
  }
}

void process_button2(void *params) {

  uint8_t state;

  while (1) {
    state = 0;
    xQueueReceive(button2_Q, &state, portMAX_DELAY);

    if (state == 1) {

      current_song_num = currentIndex;

    } else if (state == 2) {
      current_song_num++;

      if (current_song_num == 10) {
        current_song_num = 0;
      }

    } else if (state == 3) {
      current_song_num--;

      if (current_song_num == -1) {
        current_song_num = 9;
      }

    } else {
      // if received state makes it into here, do nothing
      // invalid state
    }
    xQueueSend(Song_Q, &tracklist[current_song_num], portMAX_DELAY);
    if (playback__is_playing()) {
      change_song = true;
    }
  }
}

void volume_control_task(void *params) {
  uint16_t volume;
  uint8_t channel_volume = reset_volume & 0xFF;
  bool direction;
  while (1) {

    xQueueReceive(volume_direction, &direction, portMAX_DELAY);

    if (direction) {
      if (channel_volume > MAX_VOLUME + 2) {
        channel_volume -= 0x02;
      }
    } else {
      if (channel_volume < 0xFE - 2) {
        channel_volume += 0x02;
      }
    }

    xSemaphoreTake(Decoder_lock, portMAX_DELAY);
    int volToLCD = (254 - channel_volume) / 2;
    lcd_update_volume(volToLCD);
    xSemaphoreGive(Decoder_lock);

    volume = (channel_volume << 8) | (channel_volume << 0);

    xSemaphoreTake(Decoder_lock, portMAX_DELAY);
    decoder__write_reg(0x0B, volume);
    xSemaphoreGive(Decoder_lock);
  }
}
