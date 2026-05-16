#include "middle_wifi.h"
#include "bsp/esp32_s3_matrix.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "middle_wifi";

static char sta_ssid[33] = {0};
static char sta_pass[65] = {0};
static bool inited = false;
static TaskHandle_t wifi_task_handle = NULL;
static EventGroupHandle_t wifi_event_group = NULL;
static esp_event_handler_instance_t wifi_any_id = NULL;
static esp_event_handler_instance_t ip_got_ip = NULL;
static esp_event_handler_instance_t prov_any_id = NULL;
static bool event_handlers_registered = false;
static bool prov_mgr_inited = false;
static bool provisioning_running = false;

#define WIFI_CONNECTED_BIT BIT0

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

static void prov_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != WIFI_PROV_EVENT) return;

    switch (event_id) {
    case WIFI_PROV_START:
        provisioning_running = true;
        ESP_LOGI(TAG, "SoftAP provisioning started");
        break;
    case WIFI_PROV_CRED_RECV: {
        const wifi_sta_config_t *wifi_sta_cfg = (const wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "received credentials for SSID: %s", (const char *)wifi_sta_cfg->ssid);
        break;
    }
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGW(TAG, "provisioning failed, retry from app");
        break;
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning credentials accepted");
        break;
    case WIFI_PROV_END:
        provisioning_running = false;
        if (prov_mgr_inited) {
            wifi_prov_mgr_deinit();
            prov_mgr_inited = false;
        }
        ESP_LOGI(TAG, "SoftAP provisioning ended");
        break;
    default:
        break;
    }
}

static void make_softap_service_name(char *out, size_t out_size)
{
    uint8_t mac[6] = {0};
    if (!out || out_size == 0) return;
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
        snprintf(out, out_size, "ESP32M_%02X%02X%02X", mac[3], mac[4], mac[5]);
        return;
    }
    snprintf(out, out_size, "ESP32M_SETUP");
}

static esp_err_t start_softap_provisioning(void)
{
    if (provisioning_running) return ESP_OK;

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_init(prov_cfg), TAG, "wifi_prov_mgr_init failed");
    prov_mgr_inited = true;

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_is_provisioned(&provisioned), TAG, "wifi_prov_mgr_is_provisioned failed");
    if (provisioned) {
        wifi_prov_mgr_deinit();
        prov_mgr_inited = false;
        provisioning_running = false;
        return ESP_OK;
    }

    char service_name[16] = {0};
    make_softap_service_name(service_name, sizeof(service_name));
    const char *service_key = NULL;
    const char *pop = "matrix123";

    ESP_LOGI(TAG, "starting SoftAP provisioning service: %s", service_name);
    esp_err_t r = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                                                   (const void *)pop,
                                                   service_name,
                                                   service_key);
    if (r != ESP_OK) {
        wifi_prov_mgr_deinit();
        prov_mgr_inited = false;
        provisioning_running = false;
        return r;
    }
    provisioning_running = true;
    return ESP_OK;
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
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_PROV_EVENT,
                                                             ESP_EVENT_ANY_ID,
                                                             &prov_event_handler,
                                                             NULL,
                                                             &prov_any_id),
                        TAG,
                        "register provisioning event failed");
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

    if (!cache.sta_configured) {
        esp_err_t pr = start_softap_provisioning();
        if (pr != ESP_OK) {
            cache.last_err = pr;
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
