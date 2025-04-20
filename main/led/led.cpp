#include "led.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#include "pinout.h"

#define FPS 2
static const char* TAG = "led";

void led_task(void* pvParameter) {
    bool is_on = false;
    static uint8_t led_buffer[LED_COUNT * 4] = { 0 };

    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .resolution_hz = 10000000,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };

    rmt_new_tx_channel(&tx_chan_config, &led_chan);

    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = 10000000,
    };
    rmt_new_led_strip_encoder(&encoder_config, &led_encoder);
    rmt_enable(led_chan);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };

    while (1) {
        is_on = !is_on;

        for (int i = 0; i < LED_COUNT; i++) {
            led_buffer[i * 4 + 3] = is_on;
        }

        rmt_transmit(led_chan, led_encoder, led_buffer, sizeof(led_buffer), &tx_config);
        rmt_tx_wait_all_done(led_chan, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1000 / FPS));
    }
}

void led_init(void)
{
    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);
}