#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "who_camera.h"
#include "who_cat_face_detection.hpp"
#include "main_functions.h"

// Declare both queues
static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueResult = NULL;  // Queue to receive detection results
QueueHandle_t catHeardQueue = NULL;

void tf_main(void) {
  setup();
  while (true) {
    loop();
  }
}

extern "C" void catFaceDetectionTask(void *pvParameters) {
    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueResult = xQueueCreate(1, sizeof(bool));

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2, xQueueAIFrame);
    register_cat_face_detection(xQueueAIFrame, NULL, xQueueResult, NULL, true);

    bool is_detected = false;
    while (true) {
      if (xQueueReceive(xQueueResult, &is_detected, portMAX_DELAY) && is_detected) {
        ESP_LOGI("catFaceDetectionTask", "Cat face detected!");
      }
    }
}

void catHeardQueueMonitorTask(void *parameters) {
  bool detected;
  while (true) {
    if (xQueueReceive(catHeardQueue, &detected, portMAX_DELAY) == pdTRUE && detected) {
      ESP_LOGI("catHeardQueueMonitorTask", "Meow detected!");
    }
  }
}

extern "C" void app_main() {
    // Create the queues
    catHeardQueue = xQueueCreate(10, sizeof(bool));
    if (catHeardQueue == NULL) {
        ESP_LOGE("Queue Create", "Failed to create catHeardQueue");
        return;
    }

    // Create TensorFlow task for sound detection
    xTaskCreate((TaskFunction_t)&tf_main, "tensorflow", 8 * 1024, NULL, 5, NULL);

    // Create tasks for cat face and sound detection
    xTaskCreate(catFaceDetectionTask, "catFaceDetection", 4 * 1024, NULL, 5, NULL);
    xTaskCreate(catHeardQueueMonitorTask, "catHeardQueueMonitor", 2048, NULL, 5, NULL);

    // Delete the initiator task if necessary
    vTaskDelete(NULL);
}
