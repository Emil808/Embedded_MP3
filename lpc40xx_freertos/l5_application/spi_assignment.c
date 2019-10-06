#include "FreeRTOS.h"
#include "task.h"

#include "gpio.h"
#include "semphr.h"
#include "ssp2_lab.h"
#include "uart.h"
#include "uart_printf.h"

gpio_s FM; // Flash memory, CS_flash: P1_10, CS is active low

xSemaphoreHandle flash_memory_mutex;

void adesto_cs(void) { gpio__reset(FM); };
void adesto_ds(void) { gpio__set(FM); };

typedef struct {
  uint8_t manufacturer_id;
  uint8_t device_id_1;
  uint8_t device_id_2;
  uint8_t extended_device_id;
} adesto_flash_id_s;

adesto_flash_id_s ssp2__adesto_read_signature(void) {
  adesto_flash_id_s data = {0};

  adesto_cs();
  {
    // Send opcode and read bytes

    ssp2L__exchange_byte(0x9F);                        // send opcode to read manufacturer info: 0x9F
    data.manufacturer_id = ssp2L__exchange_byte(0xFF); // recieve id
    data.device_id_1 = ssp2L__exchange_byte(0xFF);     // recieve id 1
    data.device_id_2 = ssp2L__exchange_byte(0xFF);     // recieve id 2
  }
  adesto_ds();

  return data;
}

void spi_task(void *p) {
  uint32_t spi_clock_mhz = 24;
  ssp2__init(spi_clock_mhz);
  FM = gpio__construct_as_output(GPIO__PORT_1, 10);

  uart0_init();
  while (1) {

    if (xSemaphoreTake(flash_memory_mutex, 1000)) {
      adesto_flash_id_s id = ssp2__adesto_read_signature();

      // giving back mutex here, causes print statements to be interrupted,
      // because another task (of same priority) waiting on mutex will take mutex,
      // and becomes a ready task
      // either give back mutex after prints, or change priority levels
      // For future: maybe only have one task that will only print
      uart_printf__polled(UART__0, "Manufacturer ID: %d\n", id.manufacturer_id);
      uart_printf__polled(UART__0, "Device ID-1: %d\n", id.device_id_1);
      uart_printf__polled(UART__0, "Device ID-2: %d\n", id.device_id_2);
      xSemaphoreGive(flash_memory_mutex);

    } else {
      uart_printf__polled(UART__0, "PAST WAIT TIME TO READ FROM FLASH MEMORY\n");
    }

    vTaskDelay(500);
  }
}

void main(void) {
  flash_memory_mutex = xSemaphoreCreateMutex();
  xTaskCreate(spi_task, "Read Flash Memory Info-1", (512U * 4) / sizeof(void *), NULL, 1, NULL);
  xTaskCreate(spi_task, "Read Flash Memory Info-2", (512U * 4) / sizeof(void *), NULL, 1, NULL);
  vTaskStartScheduler();
}