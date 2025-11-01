#ifndef GPS_DATA_H
#define GPS_DATA_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

// GPS data structure for SD card logging
struct sdcard_gps_data {
    uint32_t sog;
    uint32_t cog;
    uint8_t hour;
    uint8_t minute;
    uint16_t millisecond;
    uint32_t latitude;
    uint32_t longitude;
};

// GPS data structure for display
struct display_gps_data {
    uint32_t sog;
    uint32_t cog;
    uint8_t hour;
    uint8_t minute;
    uint16_t millisecond;
    bool valid;
};

// External access to message queue
extern struct k_msgq sdcard_gps_msgq;

// Functions
bool display_gps_get_current(struct display_gps_data *dest);
void gps_data_update(uint32_t sog, uint32_t cog, uint8_t hour, uint8_t minute, 
                     uint16_t millisecond, uint32_t latitude, uint32_t longitude);

#endif // GPS_DATA_H