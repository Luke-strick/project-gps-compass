/*
 * Simplified UART echo example
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/uart.h>

#include <stdio.h>
#include <zephyr/logging/log.h>

// UBX command to set 5Hz
static const uint8_t ubx_set_5hz[] = {
    0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
    0xC8, 0x00, 0x01, 0x00, 0x01, 0x00,
    0xDE, 0x6A
};

// UBX save config
static const uint8_t ubx_save_config[] = {
    0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x17, 0x31, 0xBF
};

struct sdcard_gps_data {
    uint32_t sog;
    uint32_t cog;
    uint8_t hour;
    uint8_t minute;
    uint16_t millisecond;
    uint32_t latitude;
    uint32_t longitude;
};
K_MSGQ_DEFINE(sdcard_gps_msgq, sizeof(struct sdcard_gps_data), 10, 4);


struct display_gps_data {
    uint32_t sog;
    uint32_t cog;
    uint8_t hour;
    uint8_t minute;
    uint16_t millisecond;
    bool valid;
};
static struct display_gps_data gps_data = {0};
static K_MUTEX_DEFINE(gps_data_mutex);


// LOG_MODULE_REGISTER(zephyr_gnss_sample, LOG_LEVEL_DBG);

void configure_gps_rate(void)
{
    // Get UART device that GPS is connected to
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
    
    if (!device_is_ready(uart)) {
        printk("UART device not ready");
        return;
    }
    
    printk("Configuring GPS to 5Hz...");
    
    // Send UBX command
    for (int i = 0; i < sizeof(ubx_set_5hz); i++) {
        uart_poll_out(uart, ubx_set_5hz[i]);
    }
    
    k_sleep(K_MSEC(100));  // Wait for GPS to process
    
    // Optional: Save to flash (persists across power cycles)
    printk("Saving GPS config to flash...");
    for (int i = 0; i < sizeof(ubx_save_config); i++) {
        uart_poll_out(uart, ubx_save_config[i]);
    }
    
    k_sleep(K_MSEC(500));  // Wait for save
    
    printk("GPS configuration complete");
}


bool display_gps_get_current(struct display_gps_data *dest){
    k_mutex_lock(&gps_data_mutex, K_FOREVER);
    if (gps_data.valid){
        memcpy(dest, &gps_data, sizeof(struct display_gps_data));
        k_mutex_unlock(&gps_data_mutex);
        return true;
    }
    k_mutex_unlock(&gps_data_mutex);
    return false;
}

void display_gps_invalidate(){
    k_mutex_lock(&gps_data_mutex, K_FOREVER);
    gps_data.valid = false;
    k_mutex_unlock(&gps_data_mutex);
}


static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{

	if (data->info.fix_status != GNSS_FIX_STATUS_NO_FIX) {
        k_mutex_lock(&gps_data_mutex, K_FOREVER);
        gps_data.sog = data->nav_data.speed;
        gps_data.cog = data->nav_data.bearing;
        gps_data.hour = data->utc.hour;
        gps_data.minute = data->utc.minute;
        gps_data.millisecond = data->utc.millisecond;
        gps_data.valid = true;
        k_mutex_unlock(&gps_data_mutex);

        struct sdcard_gps_data sdcard_gps_msg = {
            .sog = data->nav_data.speed,
            .cog = data->nav_data.bearing,
            .hour = data->utc.hour,
            .minute = data->utc.minute,
            .millisecond = data->utc.millisecond,
            .latitude = data->nav_data.latitude,
            .longitude = data->nav_data.longitude
        };
        k_msgq_put(&sdcard_gps_msgq, &sdcard_gps_msg, K_NO_WAIT);
	} else {
		printk("no fix. Searching sattelites...\n");
	}
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_cb);

int main(void)
{   
    struct display_gps_data display_data;
    // configure_gps_rate();
    for (;;){
        // if (k_msgq_get(&gps_msgq, &received_data, K_MSEC(100)) == 0) {
        //     LOG_INF("Speed: %d, Bearing: %d\n",
        //            received_data.sog, received_data.cog);
        // }
        if (display_gps_get_current(&display_data)){
             printk("Speed: %.3f, Bearing: %d, Time: %d:%02d:%.2f\n", (double)display_data.sog/1000.0, display_data.cog, display_data.hour, display_data.minute, (float) display_data.millisecond / 1000.0);
             display_gps_invalidate();
        }
        // k_sleep(K_MSEC(100)); 

    }
	return 0;
}