/*
 * GPS Command Parser via USART2
 * Commands:
 *   gps refresh <1|5|10>  - Set GPS update rate
 *   gps save              - Save GPS config to flash
 *   stream <on|off>       - Enable/disable GPS data streaming
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gnss.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gps_cmd, LOG_LEVEL_DBG);

// =============================================================================
// GPS DATA STRUCTURES
// =============================================================================
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

// =============================================================================
// COMMAND PARSER STATE
// =============================================================================
static bool stream_enabled = false;

// =============================================================================
// UBX COMMANDS
// =============================================================================
// Set 1Hz (1000ms)
static const uint8_t ubx_set_1hz[] = {
    0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
    0xE8, 0x03, 0x01, 0x00, 0x01, 0x00,
    0x01, 0x39
};

// Set 5Hz (200ms)
static const uint8_t ubx_set_5hz[] = {
    0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
    0xC8, 0x00, 0x01, 0x00, 0x01, 0x00,
    0xDE, 0x6A
};

// Set 10Hz (100ms)
static const uint8_t ubx_set_10hz[] = {
    0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
    0x64, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x79, 0x10
};

// Save config
static const uint8_t ubx_save_config[] = {
    0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x17, 0x31, 0xBF
};

// =============================================================================
// GPS CONFIGURATION FUNCTIONS
// =============================================================================
void gps_set_refresh_rate(int hz)
{
    const struct device *uart = DEVICE_DT_GET(DT_ALIAS(gps_usart));
    
    if (!device_is_ready(uart)) {
        printk("Error: GPS UART not ready\n");
        return;
    }
    
    const uint8_t *cmd;
    size_t cmd_len;
    
    switch(hz) {
        case 1:
            cmd = ubx_set_1hz;
            cmd_len = sizeof(ubx_set_1hz);
            break;
        case 5:
            cmd = ubx_set_5hz;
            cmd_len = sizeof(ubx_set_5hz);
            break;
        case 10:
            cmd = ubx_set_10hz;
            cmd_len = sizeof(ubx_set_10hz);
            break;
        default:
            printk("Error: Invalid refresh rate. Use 1, 5, or 10\n");
            return;
    }
    
    printk("Setting GPS refresh rate to %dHz...\n", hz);
    
    for (int i = 0; i < cmd_len; i++) {
        uart_poll_out(uart, cmd[i]);
    }
    
    k_sleep(K_MSEC(100));
    printk("GPS refresh rate set to %dHz\n", hz);
}

void gps_save_config(void)
{
    const struct device *uart = DEVICE_DT_GET(DT_ALIAS(gps_usart));
    
    if (!device_is_ready(uart)) {
        printk("Error: GPS UART not ready\n");
        return;
    }
    
    printk("Saving GPS configuration to flash...\n");
    
    for (int i = 0; i < sizeof(ubx_save_config); i++) {
        uart_poll_out(uart, ubx_save_config[i]);
    }
    
    k_sleep(K_MSEC(500));
    printk("GPS configuration saved\n");
}

// =============================================================================
// COMMAND PARSER
// =============================================================================
void process_command(char *cmd)
{
    // Trim trailing whitespace/newline
    int len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r' || cmd[len-1] == ' ')) {
        cmd[len-1] = '\0';
        len--;
    }
    
    if (len == 0) {
        return;
    }
    
    // Parse "gps refresh <rate>"
    if (strncmp(cmd, "gps refresh ", 12) == 0) {
        int rate = atoi(cmd + 12);
        if (rate == 1 || rate == 5 || rate == 10) {
            gps_set_refresh_rate(rate);
        } else {
            printk("Error: Invalid rate '%d'. Use: gps refresh <1|5|10>\n", rate);
        }
    }
    // Parse "gps save"
    else if (strcmp(cmd, "gps save") == 0) {
        gps_save_config();
    }
    // Parse "stream on"
    else if (strcmp(cmd, "stream on") == 0) {
        stream_enabled = true;
        printk("GPS streaming enabled\n");
    }
    // Parse "stream off"
    else if (strcmp(cmd, "stream off") == 0) {
        stream_enabled = false;
        printk("GPS streaming disabled\n");
    }
    // Parse "help"
    else if (strcmp(cmd, "help") == 0) {
        printk("\nAvailable commands:\n");
        printk("  gps refresh <1|5|10>  - Set GPS update rate (Hz)\n");
        printk("  gps save              - Save GPS config to flash\n");
        printk("  stream <on|off>       - Toggle GPS data streaming\n");
        printk("  help                  - Show this help\n\n");
    }
    // Unknown command
    else {
        printk("Unknown command: '%s'\n", cmd);
        printk("Type 'help' for available commands\n");
    }
}

// =============================================================================
// COMMAND INPUT THREAD
// =============================================================================
void command_thread(void)
{
    char line[128];
    int idx = 0;
    uint8_t c;
    
    const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    
    if (!device_is_ready(console)) {
        printk("ERROR: Console device not ready!\n");
        return;
    }
    
    k_sleep(K_MSEC(500));  // Give console time to initialize
    
    printk("> ");
    
    while (1) {
        if (uart_poll_in(console, &c) == 0) {
            // Handle newline
            if (c == '\n' || c == '\r') {
                printk("\n");
                line[idx] = '\0';
                
                if (idx > 0) {
                    process_command(line);
                }
                idx = 0;
                printk("> ");
            }
            // Handle backspace
            else if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    printk("\b \b");
                }
            }
            // Add to buffer
            else if (idx < sizeof(line) - 1 && c >= 32 && c <= 126) {
                line[idx++] = c;
                printk("%c", c);
            }
        } else {
            k_msleep(10);
        }
    }
}

K_THREAD_DEFINE(cmd_thread_id, 1024, command_thread, NULL, NULL, NULL, 7, 0, 0);

// =============================================================================
// GPS CALLBACK
// =============================================================================
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
        
        if (k_msgq_put(&sdcard_gps_msgq, &sdcard_gps_msg, K_NO_WAIT) != 0) {
            LOG_WRN("SD card queue full");
        }
        
        // Stream GPS data if enabled
        if (stream_enabled) {
            printk("Speed: %u.%03u m/s, Bearing: %u.%03u deg, Time: %02u:%02u:%02u.%03u\n",
                   gps_data.sog / 1000, gps_data.sog % 1000,
                   gps_data.cog / 1000, gps_data.cog % 1000,
                   gps_data.hour, gps_data.minute,
                   gps_data.millisecond / 1000, gps_data.millisecond % 1000);
        }
    }
}

GNSS_DATA_CALLBACK_DEFINE(DEVICE_DT_GET(DT_ALIAS(gnss)), gnss_data_cb);

// =============================================================================
// MAIN
// =============================================================================
int main(void)
{
    LOG_INF("GPS Application Starting");
    
    // Default GPS to 5Hz
    k_sleep(K_SECONDS(1));
    gps_set_refresh_rate(5);
    
    // Command thread handles user input
    // Main can do other work or just sleep
    while (1) {
        k_sleep(K_FOREVER);
    }
    
    return 0;
}