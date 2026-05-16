#include "middle_wifi.h"
#include "bsp/esp32_s3_matrix.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "middle_wifi";

static char sta_ssid[33] = {0};
static char sta_pass[65] = {0};
static bool inited = false;
static TaskHandle_t wifi_task_handle = NULL;
static TaskHandle_t sc_task_handle = NULL;
static EventGroupHandle_t wifi_event_group = NULL;
static esp_event_handler_instance_t wifi_any_id = NULL;
static esp_event_handler_instance_t ip_got_ip = NULL;
static esp_event_handler_instance_t sc_any_id = NULL;
static bool event_handlers_registered = false;
static bool provisioning_running = false;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_SC_DONE_BIT BIT1

static middle_wifi_status_t cache = {
    .last_err = ESP_ERR_INVALID_STATE,
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        (void)esp_wifi_connect();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && wifi_event_group) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void sc_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != SC_EVENT) return;

    if (event_id == SC_EVENT_GOT_SSID_PSWD) {
        const smartconfig_event_got_ssid_pswd_t *evt = (const smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config = {0};
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(evt->password));
        if (evt->bssid_set) {
            wifi_config.sta.bssid_set = evt->bssid_set;
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(evt->bssid));
        }
        (void)esp_wifi_disconnect();
        (void)esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        (void)esp_wifi_connect();
    }

    if (event_id == SC_EVENT_SEND_ACK_DONE && wifi_event_group) {
        xEventGroupSetBits(wifi_event_group, WIFI_SC_DONE_BIT);
    }
}

static void smartconfig_task(void *arg)
{
    ESP_LOGI(TAG, "starting ESPTouch provisioning");
    provisioning_running = true;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_SC_DONE_BIT);

    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    if (esp_smartconfig_start(&cfg) != ESP_OK) {
        provisioning_running = false;
        sc_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_SC_DONE_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);
        if (bits & WIFI_SC_DONE_BIT) {
            (void)esp_smartconfig_stop();
            break;
        }
    }

    provisioning_running = false;
    sc_task_handle = NULL;
    ESP_LOGI(TAG, "ESPTouch provisioning complete");
    vTaskDelete(NULL);
}

static esp_err_t wifi_register_handlers_once(void)
{
    if (event_handlers_registered) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                             ESP_EVENT_ANY_ID,
                                                             &wifi_event_handler,
                                                             NULL,
                                                             &wifi_any_id),
                        TAG,
                        "register wifi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                             IP_EVENT_STA_GOT_IP,
                                                             &ip_event_handler,
                                                             NULL,
                                                             &ip_got_ip),
                        TAG,
                        "register ip event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(SC_EVENT,
                                                             ESP_EVENT_ANY_ID,
                                                             &sc_event_handler,
                                                             NULL,
                                                             &sc_any_id),
                        TAG,
                        "register smartconfig event failed");
    event_handlers_registered = true;
    return ESP_OK;
}

static void wifi_reset_cache(void)
{
    cache = (middle_wifi_status_t){
        .last_err = ESP_ERR_INVALID_STATE,
    };
}

static esp_err_t wifi_info_refresh(void)
{
    middle_wifi_status_t cur = {0};
    esp_err_t r = bsp_wifi_get_status(&cur.sta_configured,
                                      &cur.sta_connected,
                                      &cur.sta_ip,
                                      &cur.sta_rssi,
                                      &cur.ap_on,
                                      &cur.ap_clients,
                                      &cur.ap_ip);
    cur.last_err = r;
    cache = cur;
    return r;
}

static void wifi_refresh_task(void *arg)
{
    while (true) {
        (void)wifi_info_refresh();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t middle_wifi_get_status(middle_wifi_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = cache;
    if (!inited) return ESP_ERR_INVALID_STATE;
    return cache.last_err;
}

void middle_wifi_set_sta_config(const char *ssid, const char *password)
{
    sta_ssid[0] = '\0';
    sta_pass[0] = '\0';
    if (ssid) {
        strncpy(sta_ssid, ssid, sizeof(sta_ssid) - 1);
    }
    if (password) {
        strncpy(sta_pass, password, sizeof(sta_pass) - 1);
    }
    wifi_reset_cache();
}

esp_err_t middle_wifi_init(void)
{
    if (inited) return ESP_OK;

    const char *ssid = sta_ssid[0] ? sta_ssid : NULL;
    const char *pass = sta_pass[0] ? sta_pass : NULL;
    wifi_reset_cache();
    esp_err_t r = bsp_init_wifi_apsta(ssid, pass);
    cache.last_err = r;
    ESP_RETURN_ON_ERROR(r, TAG, "bsp_init_wifi_apsta failed");

    if (!wifi_event_group) {
        wifi_event_group = xEventGroupCreate();
        if (!wifi_event_group) {
            bsp_wifi_stop();
            cache.last_err = ESP_ERR_NO_MEM;
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(wifi_register_handlers_once(), TAG, "wifi handler register failed");

    inited = true;
    (void)wifi_info_refresh();

    if (!cache.sta_configured && !sc_task_handle) {
        BaseType_t sc_ok = xTaskCreate(smartconfig_task,
                                       "wifi_sc_task",
                                       4096,
                                       NULL,
                                       tskIDLE_PRIORITY + 1,
                                       &sc_task_handle);
        if (sc_ok != pdPASS) {
            sc_task_handle = NULL;
            cache.last_err = ESP_FAIL;
        }
    }

    BaseType_t task_ok = xTaskCreate(wifi_refresh_task, "wifi_refresh_task", 3072, NULL,
                                     tskIDLE_PRIORITY + 1, &wifi_task_handle);
    if (task_ok == pdPASS) return ESP_OK;

    wifi_task_handle = NULL;
    inited = false;
    bsp_wifi_stop();
    cache.last_err = ESP_FAIL;
    return ESP_FAIL;
}
