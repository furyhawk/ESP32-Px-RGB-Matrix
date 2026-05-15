#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BSP_CAPS_AUDIO
#define BSP_CAPS_AUDIO 0
#endif

#ifndef BSP_SD_MOUNT_POINT
#define BSP_SD_MOUNT_POINT "/sdcard"
#endif

static inline esp_err_t bsp_i2c_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static inline i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    return NULL;
}

static inline esp_err_t bsp_sdcard_mount(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static inline esp_err_t bsp_sdcard_unmount(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static inline sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    return NULL;
}

static inline void *bsp_audio_codec_speaker_init(void)
{
    return NULL;
}

static inline void *bsp_audio_codec_microphone_init(void)
{
    return NULL;
}

static inline esp_err_t bsp_init_wifi_apsta(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    return ESP_ERR_NOT_SUPPORTED;
}

static inline void bsp_wifi_stop(void)
{
}

static inline esp_err_t bsp_wifi_get_status(bool *sta_configured,
                                            bool *sta_connected,
                                            uint32_t *sta_ip,
                                            int *sta_rssi,
                                            bool *ap_on,
                                            int *ap_clients,
                                            uint32_t *ap_ip)
{
    if (sta_configured) *sta_configured = false;
    if (sta_connected) *sta_connected = false;
    if (sta_ip) *sta_ip = 0;
    if (sta_rssi) *sta_rssi = 0;
    if (ap_on) *ap_on = false;
    if (ap_clients) *ap_clients = 0;
    if (ap_ip) *ap_ip = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

#ifdef __cplusplus
}
#endif
