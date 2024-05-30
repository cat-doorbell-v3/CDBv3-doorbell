#include "esp_log.h"
#include "who_camera.h"
#include "who_cat_face_detection.hpp"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueResult = NULL;  // Queue to receive detection results

extern "C" void app_main()
{
    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueResult = xQueueCreate(1, sizeof(bool));  // Create a queue for boolean results

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2, xQueueAIFrame);
    register_cat_face_detection(xQueueAIFrame, NULL, xQueueResult, NULL, true);

    bool is_detected = false;
    // Continuously check for detection results
    while (true) {
        if (xQueueReceive(xQueueResult, &is_detected, portMAX_DELAY)) {
            if (is_detected) {
                ESP_LOGI("app_main", "Cat face detected!");
            } 
        }
    }
}
