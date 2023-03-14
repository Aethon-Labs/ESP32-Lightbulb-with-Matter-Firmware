#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_smartconfig.h>
#include <esp_mqtt.h>

#include "chip.h"
#include "platform/CHIPDeviceLayer.h"
#include "support/ErrorStr.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MQTT_CONNECTED_BIT BIT2

#define TAG "lightbulb"

static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;

static esp_mqtt_client_handle_t mqtt_client;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Wi-Fi started");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wi-Fi connected");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "Wi-Fi disconnected");
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        break;
    default:
        break;
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        break;
    default:
        break;
    }
}

static void wifi_smartconfig_task(void *params)
{
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Wi-Fi connected");
        esp_smartconfig_stop();
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Wi-Fi connection failed");
    }

    vTaskDelete(NULL);
}

static void mqtt_publish_task(void *params)
{
    esp_mqtt_client_publish(mqtt_client, "/lightbulb/status", "on", 0, 1, 0);
    vTaskDelete(NULL);
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid, "your_ssid_here");
    strcpy((char *)wifi_config.sta.password, "your_password_here");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    // Start SmartConfig if Wi-Fi is not already connected
    if (esp_wifi_connect() != ESP_OK)
    {
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        ESP_ERROR_CHECK(esp_smartconfig_start(wifi_smartconfig_task));
    }

    // Initialize Matter
    CHIP_ERROR err = chip::Platform::MemoryInit();
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "MemoryInit() failed: %s", chip::ErrorStr(err));
        abort();
    }

    err = chip::DeviceLayer::PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "InitChipStack() failed: %s", chip::ErrorStr(err));
        abort();
    }

    err = chip::DeviceLayer::ConnectivityMgr().Init();
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "ConnectivityMgr().Init() failed: %s", chip::ErrorStr(err));
        abort();
    }

    // Initialize MQTT
    mqtt_event_group = xEventGroupCreate();
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://your_broker_url_here",
        .event_handle = mqtt_event_handler,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    // Wait for Wi-Fi and MQTT to connect
    xEventGroupWaitBits(mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // Publish initial state to MQTT
    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 2048, NULL, 5, NULL);

    // Main loop
    while (1)
    {
        // Run the CHIP device event loop
        chip::DeviceLayer::PlatformMgr().RunEventLoop();
    }
}   
