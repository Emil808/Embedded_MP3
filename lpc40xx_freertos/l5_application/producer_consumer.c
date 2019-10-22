/*Observations
    Producer: Low
    Consumer: High
    Consumer task will wait on its queuereceive, and will come back once producer sends something to the queue
    Consumer will print "Getting switch value" first
    then the queuereceive will try to read from queue but it is empty, so it will sleep

    the producer can then send, so it prints "Sending value"
    once it sends, the consumer will take over and prints "received value" and sleeps on the task delay
    which then lets the producer send "Sent Value"

    Producer: High
    Consumer: Low
    With Producer at Higher Priority, it prints its "sending" and "sent" messages, then reaches its taskdelay and sleeps
    which allows Consumer at Lower priority to print its "Get" and "Receive" messages

    Producer and Consumer Same Priority
    The consumer tasks sleeps on its queue receive,
    the producer tasks sends the value, but is not switched out by the consumer

    the print messages take turns printing
    "Getting Value" then sleeps at queue receive
    "Sending Value" then "Value Sent" prints
    then "Got Value" prints

    0 Delay on QueueReceive, with both same priority
    console spammed with
    "GET" "Received" messages because it has no task delay
    after time slice, producer runs,
    producer runs, but print statements gets interrupted with consumer's messages

    block timing in the Queue Receive will make the task wait within a given time,
    if the time runs out, then the task could go into a "Didn't Receive" condition
    if within the time, the queue gets data to be read, the task can go into a "Received data" condition.
    The producer makes data that is essential for a consumer to use that data,
    having the blocking time forces the consumer to wait for data to be available
    this will prevent having to design a "Didn't Receive data" condition, which could make the progarm more complex

 */

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
    // This xQueueSend() will internally switch context to "consumer" task because it is higher priority than this
    // "producer" task Then, when the consumer task sleeps, we will resume out of xQueueSend()and go over to the next
    // line

    switch_value = get_switch_input_from_switch0();

    uart_printf(UART__0, "Sending switch value to queue\n");

    xQueueSend(switch_queue, &switch_value, 1000);

    uart_printf(UART__0, "Switch value sent!\n");

    vTaskDelay(1000);
  }
}

void consumer(void *p) {
  switch_state x;
  while (1) {
    uart_printf(UART__0, "Getting switch value from queue\n");
    xQueueReceive(switch_queue, &x, 1000);
    uart_printf(UART__0, "Got switch value: %d\n", x);
  }
}

void main(void) {
  uart0_init();

  switch0 = gpio__construct_as_input(GPIO__PORT_1, 19);
  LPC_IOCON->P1_19 &= ~(3 << 3);
  LPC_IOCON->P1_19 |= (1 << 3);

  switch_queue = xQueueCreate(1, sizeof(switch_state));

  sj2_cli__init();
  TaskHandle_t prod, cons;
  xTaskCreate(producer, "Producer", (512U * 4) / sizeof(void *), NULL, 2, &prod);
  xTaskCreate(consumer, "Consumer", (512U * 4) / sizeof(void *), NULL, 2, &cons);

  vTaskStartScheduler();
}