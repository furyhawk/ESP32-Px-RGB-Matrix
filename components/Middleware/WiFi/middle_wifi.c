#include "middle_wifi.h"
#include "bsp/esp32_s3_matrix.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
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
    if (event_base != NETWORK_PROV_EVENT) return;

    switch (event_id) {
    case NETWORK_PROV_START:
        provisioning_running = true;
        ESP_LOGI(TAG, "provisioning started");
        break;
    case NETWORK_PROV_WIFI_CRED_RECV: {
        const wifi_sta_config_t *wifi_sta_cfg = (const wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "received credentials for SSID: %s", (const char *)wifi_sta_cfg->ssid);
        break;
    }
    case NETWORK_PROV_WIFI_CRED_FAIL: {
        const network_prov_wifi_sta_fail_reason_t *reason = (const network_prov_wifi_sta_fail_reason_t *)event_data;
        const char *reason_str = "unknown";
        if (reason) {
            reason_str = (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR)
                             ? "Wi-Fi auth failed"
                             : "Wi-Fi AP not found";
        }
        ESP_LOGW(TAG, "provisioning failed: %s", reason_str);
        /* Mirrors Espressif example behavior: allow retry without rebooting. */
        network_prov_mgr_reset_wifi_sm_state_on_failure();
        break;
    }
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning credentials accepted");
        break;
    case NETWORK_PROV_END:
        provisioning_running = false;
        if (prov_mgr_inited) {
            network_prov_mgr_deinit();
            prov_mgr_inited = false;
        }
        ESP_LOGI(TAG, "provisioning ended");
        break;
    default:
        break;
    }
}

static esp_err_t start_ble_provisioning(void)
{
    if (provisioning_running) return ESP_OK;

    network_prov_mgr_config_t prov_cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(prov_cfg), TAG, "network_prov_mgr_init failed");
    prov_mgr_inited = true;

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned), TAG, "network_prov_mgr_is_wifi_provisioned failed");
    if (provisioned) {
        network_prov_mgr_deinit();
        prov_mgr_inited = false;
        provisioning_running = false;
        return ESP_OK;
    }

    char service_name[16] = {0};
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(service_name, sizeof(service_name), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        snprintf(service_name, sizeof(service_name), "PROV_MATRIX");
    }
    const char *service_key = NULL;

    ESP_LOGI(TAG, "starting BLE provisioning service: %s", service_name);
    esp_err_t r = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_0,
                                                   NULL,
                                                   service_name,
                                                   service_key);
    if (r != ESP_OK) {
        network_prov_mgr_deinit();
        prov_mgr_inited = false;
        provisioning_running = false;
        return r;
    }
    provisioning_running = true;
    return ESP_OK;
}

esp_err_t middle_wifi_get_prov_service_name(char *buf, size_t len)
{
    if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(buf, len, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        snprintf(buf, len, "PROV_MATRIX");
    }
    return ESP_OK;
}

bool middle_wifi_is_provisioning_running(void)
{
    return provisioning_running;
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
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(NETWORK_PROV_EVENT,
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
        esp_err_t pr = start_ble_provisioning();
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
