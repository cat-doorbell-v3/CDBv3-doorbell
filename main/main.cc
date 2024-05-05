#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "main_functions.h"

QueueHandle_t catHeardQueue = NULL;

void tf_main(void) {
  setup();
  while (true) {
    loop();
  }
}

// Task to monitor catHeardQueue
void catHeardQueueMonitorTask(void *parameters) {
  bool detected;
  while (true) {
    if (xQueueReceive(catHeardQueue, &detected, portMAX_DELAY) == pdTRUE && detected) {
      ESP_LOGI("QueueMonitor", "Meow detected!");
    }
  }
}

extern "C" void app_main() {
  // Create the queue
  catHeardQueue = xQueueCreate(10, sizeof(bool));
  if (catHeardQueue == NULL) {
    ESP_LOGE("Queue Create", "Failed to create catHeardQueue");
    return; // Queue creation failed
  }

  // Create TensorFlow task
  xTaskCreate((TaskFunction_t)&tf_main, "tensorflow", 8 * 1024, NULL, 5, NULL);

  // Create a task to listen for messages on the catHeardQueue
  xTaskCreate(catHeardQueueMonitorTask, "catHeardQueueMonitor", 2048, NULL, 5, NULL);

  // Delete the initiator task
  vTaskDelete(NULL);
}
