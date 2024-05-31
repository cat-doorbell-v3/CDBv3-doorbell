#include "esp_log.h"
#include "who_camera.h"
#include "who_cat_face_detection.hpp"

#include "nvs_flash.h"

#include "config_manager.h"
#include "wifi_manager.h"

#define LOG_TAG "app_main"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueResult = NULL;  // Queue to receive detection results
std::string doorbellRingUrl = "";
int connection_attempts = 0;

extern "C" void app_main()
{
    ESP_LOGI(LOG_TAG,"Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_LOGI(LOG_TAG,"Initializing WiFi...");

    WifiManager& wifiManager = WifiManager::getInstance();

    wifiManager.init();

    ESP_LOGI(LOG_TAG,"Initializing config...");

    auto& config = json_config::getInstance();
    config.initialize();

    doorbellRingUrl = config.get("doorbell_ring_notify_url");

    std::string macAddress = wifiManager.getMacAddress();

    std::string hostName = config.get("wifi_hostname");

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

    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueResult = xQueueCreate(1, sizeof(bool));  // Create a queue for boolean results

    register_camera(PIXFORMAT_RGB565, FRAMESIZE_240X240, 2, xQueueAIFrame);
    register_cat_face_detection(xQueueAIFrame, NULL, xQueueResult, NULL, true);

    bool is_detected = false;
    // Continuously check for detection results
    while (true) {
        if (xQueueReceive(xQueueResult, &is_detected, portMAX_DELAY) && is_detected) {
            ESP_LOGI("app_main", "Cat face detected!");
            is_detected = false;
        }
    }
}