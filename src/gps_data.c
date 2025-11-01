#include "gps_data.h"
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gps_data, LOG_LEVEL_DBG);

// Message queue for SD card logging
K_MSGQ_DEFINE(sdcard_gps_msgq, sizeof(struct sdcard_gps_data), 10, 4);

// Current GPS state
static struct display_gps_data gps_data = {0};
static K_MUTEX_DEFINE(gps_data_mutex);

bool display_gps_get_current(struct display_gps_data *dest)
{
    k_mutex_lock(&gps_data_mutex, K_FOREVER);
    if (gps_data.valid) {
        memcpy(dest, &gps_data, sizeof(struct display_gps_data));
        k_mutex_unlock(&gps_data_mutex);
        return true;
    }
    k_mutex_unlock(&gps_data_mutex);
    return false;
}

void gps_data_update(uint32_t sog, uint32_t cog, uint8_t hour, uint8_t minute,
                     uint16_t millisecond, uint32_t latitude, uint32_t longitude)
{
    // Update displayed data
    k_mutex_lock(&gps_data_mutex, K_FOREVER);
    gps_data.sog = sog;
    gps_data.cog = cog;
    gps_data.hour = hour;
    gps_data.minute = minute;
    gps_data.millisecond = millisecond;
    gps_data.valid = true;
    k_mutex_unlock(&gps_data_mutex);

    // Add to queue for SD card logging
    struct sdcard_gps_data sdcard_msg = {
        .sog = sog,
        .cog = cog,
        .hour = hour,
        .minute = minute,
        .millisecond = millisecond,
        .latitude = latitude,
        .longitude = longitude
    };
    
    if (k_msgq_put(&sdcard_gps_msgq, &sdcard_msg, K_NO_WAIT) != 0) {
        LOG_WRN("SD card queue full");
    }
}