/***************************
 * Interrupt Assignment P0 *
 * Emil Agonoy *
 * August 15, 2019 *
 * CMPE 146 *
 * ************************/
#include "FreeRTOS.h"
#include "delay.h"
#include "gpio.h"
#include "gpio_int.h"
#include "lpc40xx.h"
#include "lpc_peripherals.h"
#include "semphr.h"
#include "task.h"
#include "uart.h"
#include "uart_printf.h"
xSemaphoreHandle switch_signal;
void sleep_on_sem_task(void *led) {
  gpio__set_as_output(*(gpio_s *)led);

  while (1) {
    if (xSemaphoreTake(switch_signal, portMAX_DELAY)) {

      uart_printf__polled(UART__0, "GOT A SEMAPHORE\n");
      gpio__toggle(*(gpio_s *)led);
    }
    uart_printf__polled(UART__0, "1\n");
    vTaskDelay(100);
  }
}
void test_gpio_toggle(void *led) {

  while (1) {
    gpio__toggle(*(gpio_s *)led);
    vTaskDelay(150);
  }
}
void gpio_interrupt_semphr(void) {
  // Using interrupt to give binary semaphore
  LPC_GPIOINT->IO0IntClr |= (1 << 30);
  xSemaphoreGiveFromISR(switch_signal, NULL);
}
void gpio_interrupt(void) {
  // Intterupt that toggles led3
  LPC_GPIOINT->IO0IntClr |= (1 << 29);
  gpio_s led = gpio__construct_as_output(1, 18);
  gpio__toggle(led);
}
static gpio_s led2;
static gpio_s led0;
void part0_code(void) {
  // for part 0, normal interrupt

  gpio_s sw3 = gpio__construct(0, 29);
  gpio__set_as_input(sw3);
  LPC_GPIOINT->IO0IntEnF |= (1 << sw3.pin_number);
  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio_interrupt);
  led0 = gpio__construct_as_output(2, 3);
  xTaskCreate(test_gpio_toggle, "Blink LED", (512U / sizeof(void *)), &led0, 1, NULL);
  vTaskStartScheduler();
}
void part1_code(void) {
  switch_signal = xSemaphoreCreateBinary();
  gpio_s sw2 = gpio__construct_as_input(0, 30);
  LPC_GPIOINT->IO0IntEnF |= (1 << sw2.pin_number);
  lpc_peripheral__enable_interrupt(LPC_PERIPHERAL__GPIO, gpio_interrupt_semphr);
  led2 = gpio__construct(1, 24);
  led0 = gpio__construct_as_output(2, 3);
  xTaskCreate(sleep_on_sem_task, "sem", (512U * 4) / sizeof(void *), &led2, PRIORITY_LOW, NULL);
  xTaskCreate(test_gpio_toggle, "Blink LED", (512U / sizeof(void *)), &led0, 1, NULL);
  vTaskStartScheduler();
}
void part2_code(void) {
  // led 0 blink forever
  // led3, led2, will toggle when sw3 or sw2 interrupt comes in
  // currently, will get stuck in interrupt handler

  // Using Interrupt to give binary semaphore method
  switch_signal = xSemaphoreCreateBinary();
  // part 2, my own gpio interrupt driver
  gpio_s sw2 = gpio__construct_as_input(0, 30);
  gpio_s sw3 = gpio__construct(0, 29);
  gpio__set_as_input(sw3);
  gpio0__attach_interrupt(30, GPIO_INTR__FALLING_EDGE, gpio_interrupt_semphr);
  gpio0__attach_interrupt(29, GPIO_INTR__FALLING_EDGE, gpio_interrupt);
  led2 = gpio__construct(1, 24);
  led0 = gpio__construct_as_output(2, 3);
  xTaskCreate(sleep_on_sem_task, "sem", (512U * 4) / sizeof(void *), &led2, PRIORITY_LOW, NULL);
  xTaskCreate(test_gpio_toggle, "Blink LED", (512U / sizeof(void *)), &led0, 1, NULL);
  vTaskStartScheduler();
}
int main3(void) {

  // part0_code();
  // part1_code();
  uart0_init();
  part2_code();
}
