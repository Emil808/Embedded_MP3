#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_io.h"
#include "common_macros.h"
#include "delay.h"
#include "gpio.h"
#include "i2c.h"
#include "i2c_slave.h"
#include "sj2_cli.h"

static void blink_task(void *params);
static void uart_task(void *params);
static void blink_on_startup(gpio_s gpio, int count);

static gpio_s led0, led1;

volatile char slave_memory[256];

bool i2c__slave_send_data_to_master(uint8_t memory_index, volatile uint32_t *memory) {

  if (memory_index < 256) {
    *memory = slave_memory[memory_index];
    return true;
  }
  // memory index invalid, don't save, return false
  else
    return false;
}

bool i2c__slave_receive_data_from_master(uint8_t memory_index, uint8_t memory_value) {
  if (memory_index < 256) {
    slave_memory[memory_index] = memory_value;
    return true;
  } else
    return false;
}

void i2c__slave_receive_index_from_master(uint8_t *index, uint8_t index_value) { *index = index_value; }

int main(void) {
  // Construct the LEDs and blink a startup sequence
  led0 = board_io__get_led0();
  led1 = board_io__get_led1();
  blink_on_startup(led1, 2);

  // pin set up for master and slave devices
  gpio__construct_with_function(GPIO__PORT_0, 10, GPIO__FUNCTION_2);
  gpio__construct_with_function(GPIO__PORT_0, 11, GPIO__FUNCTION_2);
  LPC_IOCON->P0_10 &= ~(3 << 3);
  LPC_IOCON->P0_11 &= ~(3 << 3);
  LPC_IOCON->P0_10 |= (1 << 10);
  LPC_IOCON->P0_11 |= (1 << 10);

  // only for slave device

  i2c__slave_init(I2C__2, 0x40);
  slave_memory[0] = 5;

  xTaskCreate(blink_task, "led0", configMINIMAL_STACK_SIZE, (void *)&led0, PRIORITY_LOW, NULL);
  xTaskCreate(blink_task, "led1", configMINIMAL_STACK_SIZE, (void *)&led1, PRIORITY_LOW, NULL);

  // It is advised to either run the uart_task, or the SJ2 command-line (CLI), but not both
  // Change '#if 0' to '#if 1' and vice versa to try it out
#if 0
  // printf() takes more stack space, size this tasks' stack higher
  xTaskCreate(uart_task, "uart", (512U * 8) / sizeof(void *), NULL, PRIORITY_LOW, NULL);
#else
  sj2_cli__init();

  UNUSED(uart_task); // uart_task is un-used in if we are doing cli init()
#endif

  puts("Starting RTOS");
  vTaskStartScheduler(); // This function never returns unless RTOS scheduler runs out of memory and fails

  return 0;
}

static void blink_task(void *params) {
  const gpio_s led = *((gpio_s *)params);
  slave_memory[0] = 1;
  // Warning: This task starts with very minimal stack, so do not use printf() API here to avoid stack overflow
  while (true) {
    // gpio__toggle(led);

    if (slave_memory[0]) {
      gpio__toggle(led);
    } else {
      gpio__set(led);
    }

    vTaskDelay(500);
  }
}

// This sends periodic messages over printf() which uses system_calls.c to send them to UART0
static void uart_task(void *params) {
  TickType_t previous_tick = 0;
  TickType_t ticks = 0;

  while (true) {
    // This loop will repeat at precise task delay, even if the logic below takes variable amount of ticks
    vTaskDelayUntil(&previous_tick, 2000);

    /* Calls to fprintf(stderr, ...) uses polled UART driver, so this entire output will be fully
     * sent out before this function returns. See system_calls.c for actual implementation.
     *
     * Use this style print for:
     *  - Interrupts because you cannot use printf() inside an ISR
     *  - During debugging in case system crashes before all output of printf() is sent
     */
    ticks = xTaskGetTickCount();
    fprintf(stderr, "%u: This is a polled version of printf used for debugging ... finished in", (unsigned)ticks);
    fprintf(stderr, " %lu ticks\n", (xTaskGetTickCount() - ticks));

    /* This deposits data to an outgoing queue and doesn't block the CPU
     * Data will be sent later, but this function would return earlier
     */
    ticks = xTaskGetTickCount();
    printf("This is a more efficient printf ... finished in");
    printf(" %lu ticks\n\n", (xTaskGetTickCount() - ticks));
  }
}

static void blink_on_startup(gpio_s gpio, int blinks) {
  const int toggles = (2 * blinks);
  for (int i = 0; i < toggles; i++) {
    delay__ms(250);
    gpio__toggle(gpio);
  }
}
