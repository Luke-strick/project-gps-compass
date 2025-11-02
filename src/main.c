#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "data_handler.h"
#include "gps_config.h"
#include "command_parser.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
    if (data->info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
        // Update GPS data
        struct gps_data g_data = {
            data->nav_data.speed,
            data->nav_data.bearing,
            data->utc.hour,
            data->utc.minute,
            data->utc.millisecond,
            data->nav_data.latitude,
            data->nav_data.longitude,
            true,
            true
        };
        set_gps_data(g_data);
        
        // Stream if enabled
        if (command_parser_is_streaming()) {
            printk("%02u:%02u:%02u.%03u sog: %u.%03u m/s, cog: %u.%03u deg\n",
                    g_data.hour, g_data.minute,
                    g_data.millisecond / 1000, g_data.millisecond % 1000,
                    g_data.sog / 1000, g_data.sog % 1000,
                    g_data.cog / 1000, g_data.cog % 1000);
        }
    }
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_cb);

int main(void)
{
    LOG_INF("GPS Application Starting");
    
    // Initialize command parser
    command_parser_init();
    
    // Default GPS to 5Hz
    k_sleep(K_SECONDS(1));
    gps_enable_standard_messages();

    invalidate_sensor_data();
    
    // Main loop - can add other tasks here
    while (1) {
        k_sleep(K_FOREVER);
    }
    
    return 0;
}