/*
 * Simplified UART echo example
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>

#define DEV_CONSOLE DEVICE_DT_GET(DT_CHOSEN(zephyr_console))
#define DEV_OTHER   DEVICE_DT_GET(DT_CHOSEN(uart_passthrough))

static void uart_cb(const struct device *dev, void *ctx)
{
    uint8_t c;
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        if (uart_fifo_read(dev, &c, 1) > 0) {
            printk("%c", c);  // print received character
        }
    }
}

int main(void)
{
    printk("Console Device: %p\n", DEV_CONSOLE);
    printk("Other Device:   %p\n", DEV_OTHER);

    uart_irq_callback_user_data_set(DEV_CONSOLE, uart_cb, NULL);
    uart_irq_callback_user_data_set(DEV_OTHER, uart_cb, NULL);

    uart_irq_rx_enable(DEV_CONSOLE);
    uart_irq_rx_enable(DEV_OTHER);

    for (;;) {
		// printk("here");
        k_sleep(K_MSEC(100));
    }

    return 0;
}
