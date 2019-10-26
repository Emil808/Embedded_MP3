#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "gpio.h"
#include "lpc40xx.h"
#include "uart.h"
#include "uart_printf.h"

#include "sj2_cli.h"

static QueueHandle_t switch_queue;

gpio_s switch0;

typedef enum { switch_off, switch_on } switch_state;

switch_state get_switch_input_from_switch0() {
  // read switch0
  if (gpio__get(switch0)) {
    return switch_on;
  } else {
    return switch_off;
  }
}
void producer(void *p) {
  switch_state switch_value = switch_off;
  while (1) {

    switch_value = get_switch_input_from_switch0();

    uart_printf(UART__0, "Sending switch value: %d to queue\n", switch_value);

    xQueueSend(switch_queue, &switch_value, portMAX_DELAY);

    uart_printf(UART__0, "Switch value sent!\n");

    vTaskDelay(1000);
  }
}

void consumer(void *p) {
  switch_state x;
  while (1) {
    uart_printf(UART__0, "Getting switch value from queue\n");
    xQueueReceive(switch_queue, &x, portMAX_DELAY);
    uart_printf(UART__0, "Got switch value: %d\n", x);
  }
}

void main(void) {
  uart0_init();

  switch0 = gpio__construct_as_input(GPIO__PORT_1, 19);
  LPC_IOCON->P1_19 &= ~(3 << 3);
  LPC_IOCON->P1_19 |= (1 << 3);

  switch_queue = xQueueCreate(2, sizeof(switch_state));

  sj2_cli__init();
  TaskHandle_t prod, cons;
  xTaskCreate(producer, "Producer", (512U * 4) / sizeof(void *), NULL, 2, &prod);
  xTaskCreate(consumer, "Consumer", (512U * 4) / sizeof(void *), NULL, 1, &cons);

  vTaskStartScheduler();
}