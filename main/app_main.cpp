#include "esp_log.h"
#include "who_camera.h"
#include "who_cat_face_detection.hpp"

#include "nvs_flash.h"
#include "esp_timer.h"

#include "config_manager.h"
#include "wifi_manager.h"
#include "http_manager.h"

/*
Todo: Save snapshot to SD card on positive detection
Todo: Use mac address for identification
Todo: Include hostname in the POST request
Todo: Show a message on the display when a cat face is detected or maybe the snapshot?
Todo: Add a button to the device to manually trigger the doorbell ring?
Todo: Show real-time video feed on the display (possible?)
*/
#define LOG_TAG "app_main"
#define TEN_MINUTES_MICROSECONDS 600000000L // 10 minutes in microseconds

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueResult = NULL;  // Queue to receive detection results
std::string doorbellRingUrl = "";
int connection_attempts = 0;
int64_t last_post_time = -TEN_MINUTES_MICROSECONDS;  // Initialize to a value that ensures the first detection is processed

extern "C" void app_main()
{
    ESP_LOGI(LOG_TAG,"Initializing NVS...");
    if (esp_err_t ret = nvs_flash_init(); ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    ESP_LOGI(LOG_TAG,"Initializing WiFi...");

    WifiManager& wifiManager = WifiManager::getInstance();

    wifiManager.init();

    ESP_LOGI(LOG_TAG,"Initializing config...");

    auto& config = json_config::getInstance();
    config.initialize();

    doorbellRingUrl = config.get("doorbell_ring_notify_url");

    std::string macAddress = wifiManager.getMacAddress();

    config.set_context(macAddress.c_str());

    std::string hostName = config.get("wifi_hostname");

    config.reset_context();

    wifiManager.setHostname(hostName.c_str());

    ESP_LOGI(LOG_TAG, "Connecting WiFi:\nHostname: %s\nMAC address: %s\nSSID: %s\nPassword: %s", 
         hostName.c_str(), macAddress.c_str(), config.get("wifi_ssid").c_str(), config.get("wifi_password").c_str());

    auto wifi_retry_max_count = static_cast<size_t>(std::stoull(config.get("wifi_retry_max_count")));

    wifiManager.connect(
        config.get("wifi_ssid").c_str(), 
        config.get("wifi_password").c_str(),
        wifi_retry_max_count
    );

    while(!wifiManager.isConnected()) {
        ESP_LOGI(LOG_TAG,"Waiting for WiFi connection...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        connection_attempts++;
        if (connection_attempts > 10) {
            ESP_LOGE(LOG_TAG,"Could not connect to WiFi. Restarting.");
            esp_restart();
        }
    }
    ESP_LOGI(LOG_TAG,"WiFi connected");

    HttpManager& httpManager = HttpManager::getInstance();

    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueResult = xQueueCreate(1, sizeof(bool));  // Create a queue for boolean results

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_VGA, 2, xQueueAIFrame);
    register_cat_face_detection(xQueueAIFrame, NULL, xQueueResult, NULL, true);

    bool is_detected = false;
    // Continuously check for detection results
    while (true) {
        if (xQueueReceive(xQueueResult, &is_detected, portMAX_DELAY) && is_detected) {
            ESP_LOGI("app_main", "Cat face detected!");

            // Check if at least 10 minutes have passed since the last post
            if (int64_t current_time = esp_timer_get_time(); current_time - last_post_time >= TEN_MINUTES_MICROSECONDS) {
                httpManager.postToAwsApiGateway(doorbellRingUrl, "");
                last_post_time = current_time;  // Update the last post time
            } else {
                ESP_LOGI("app_main", "Detection ignored, less than 10 minutes since last notification.");
            }

            is_detected = false;
        }
    }
}