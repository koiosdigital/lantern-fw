#include "led.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#include <esp_wifi.h>
#include "wifi_provisioning/manager.h"
#include "protocomm_ble.h"
#include "kd_common.h"
#include "pinout.h"

#define FPS 20
static const char* TAG = "led";

LEDEffect_t current_effect = LED_OFF;
static uint8_t led_brightness = 255;
static uint8_t led_color[3] = { 0, 0, 0 };
static bool fading_out = false;
static uint32_t blink_changed_at = 0;
static bool blink_state = false;
static bool fading_in = false;

static uint8_t led_buffer[LED_COUNT * 4] = { 0 };

void led_set_effect(LEDEffect_t effect) {
    current_effect = effect;
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    led_color[0] = r;
    led_color[1] = g;
    led_color[2] = b;
}

void led_set_brightness(uint8_t brightness) {
    led_brightness = brightness;
}

void led_fade_out() {
    fading_out = true;
    fading_in = false;
}

void led_fade_in() {
    fading_in = true;
    fading_out = false;
}

void handle_fading() {
    if (fading_out) {
        led_brightness -= 5;
        if (led_brightness <= 0) {
            led_brightness = 0;
            fading_out = false;
        }
    }
    else if (fading_in) {
        led_brightness += 5;
        if (led_brightness >= 255) {
            led_brightness = 255;
            fading_in = false;
        }
    }
}

void tx_buf_fill_color(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) {
        led_buffer[i * 4 + 0] = g;
        led_buffer[i * 4 + 1] = r;
        led_buffer[i * 4 + 2] = b;
        led_buffer[i * 4 + 3] = 0;
    }
}

void tx_buf_set_color_at(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= LED_COUNT) {
        ESP_LOGE(TAG, "Index out of bounds");
        return;
    }
    led_buffer[index * 4 + 0] = g;
    led_buffer[index * 4 + 1] = r;
    led_buffer[index * 4 + 2] = b;
    led_buffer[index * 4 + 3] = 0;
}

void led_blink() {
    if (xTaskGetTickCount() - blink_changed_at > pdMS_TO_TICKS(500)) {
        blink_state = !blink_state;
        if (blink_state) {
            tx_buf_fill_color(led_color[0], led_color[1], led_color[2]);
        }
        else {
            tx_buf_fill_color(0, 0, 0);
        }
        blink_changed_at = xTaskGetTickCount();
    }
}

void led_breathe() {
    static uint8_t brightness = 0;
    static bool increasing = true;

    if (increasing) {
        brightness += 5;
        if (brightness >= 255) {
            brightness = 255;
            increasing = false;
        }
    }
    else {
        brightness -= 5;
        if (brightness <= 0) {
            brightness = 0;
            increasing = true;
        }
    }

    tx_buf_fill_color(led_color[0] * brightness / 255, led_color[1] * brightness / 255, led_color[2] * brightness / 255);
}

//LEDs are arranged in a circle, loading spinner effect
void led_cyclic() {
    static uint8_t offset = 0;
    static uint32_t last_update = 0;
    static int trail_size = 5;

    if (xTaskGetTickCount() - last_update > pdMS_TO_TICKS(100)) {
        offset = (offset + 1) % LED_COUNT;
        for (int i = 0; i < LED_COUNT; i++) {
            if (i < trail_size) {
                tx_buf_set_color_at((i + offset) % LED_COUNT, led_color[0], led_color[1], led_color[2]);
            }
            else {
                tx_buf_set_color_at((i + offset) % LED_COUNT, 0, 0, 0);
            }
        }
        last_update = xTaskGetTickCount();
    }
}

void led_loop() {
    handle_fading();

    switch (current_effect) {
    case LED_OFF:
        tx_buf_fill_color(0, 0, 0);
        break;
    case LED_SOLID:
        tx_buf_fill_color(led_color[0] * led_brightness / 255, led_color[1] * led_brightness / 255, led_color[2] * led_brightness / 255);
        break;
    case LED_BLINK:
        led_blink();
        break;
    case LED_BREATHE:
        led_breathe();
        break;
    case LED_CYCLIC:
        led_cyclic();
        break;
    case LED_RAINBOW:
        // Implement rainbow effect here
        break;
    }
}

void led_task(void* pvParameter) {
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
        led_loop();

        rmt_transmit(led_chan, led_encoder, led_buffer, sizeof(led_buffer), &tx_config);
        rmt_tx_wait_all_done(led_chan, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1000 / FPS));
    }
}

TaskHandle_t prov_led_task_handle = NULL;
void prov_led_task(void* pvParameter) {
    char* popToken = kd_common_provisioning_get_pop_token();
    uint8_t currentChar = 0;
    while (1) {
        if (popToken[currentChar] == '\0') {
            currentChar = 0;
            led_set_effect(LED_OFF);
            led_set_color(0, 0, 0);
            led_set_brightness(0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        switch (popToken[currentChar]) {
        case '1':
            led_set_color(255, 0, 0);
            break;
        case '2':
            led_set_color(0, 255, 0);
            break;
        case '3':
            led_set_color(0, 0, 255);
            break;
        case '4':
            led_set_color(255, 255, 0);
            break;
        case '5':
            led_set_color(255, 0, 255);
            break;
        case '6':
            led_set_color(0, 255, 255);
            break;
        default:
            led_set_color(255, 255, 255);
        }
        led_set_effect(LED_SOLID);
        led_set_brightness(255);
        vTaskDelay(pdMS_TO_TICKS(1000));
        led_set_effect(LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(500));

        currentChar++;
    }
}

void wifi_prov_connected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (prov_led_task_handle) {
        vTaskDelete(prov_led_task_handle);
        prov_led_task_handle = NULL;
    }
    xTaskCreate(prov_led_task, "prov_led_task", 4096, NULL, 5, &prov_led_task_handle);
}

void wifi_prov_disconnected(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (prov_led_task_handle) {
        vTaskDelete(prov_led_task_handle);
        prov_led_task_handle = NULL;
    }
    led_set_effect(LED_OFF);
}

void wifi_prov_started(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    led_set_effect(LED_CYCLIC);
    led_set_color(255, 127, 0);
    led_set_brightness(255);
}

void provisioning_event_handler2(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    static int wifiConnectionAttempts = 0;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START: {
            wifi_config_t wifi_cfg;
            esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

            if (strlen((const char*)wifi_cfg.sta.ssid) != 0) {
                led_set_effect(LED_CYCLIC);
                led_set_color(0, 0, 255);
                led_set_brightness(255);
                break;
            }
            break;
        }

        }
    }
    else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            led_set_effect(LED_OFF);
        }
    }
}

void led_init(void)
{
    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);

    //Display QR code once connected to endpoint device
    esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, PROTOCOMM_TRANSPORT_BLE_CONNECTED, &wifi_prov_connected, NULL);
    esp_event_handler_register(WIFI_PROV_EVENT, WIFI_PROV_START, &wifi_prov_started, NULL);
    esp_event_handler_register(WIFI_PROV_EVENT, WIFI_PROV_END, &wifi_prov_disconnected, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &provisioning_event_handler2, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &provisioning_event_handler2, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));

    led_set_effect(LED_SOLID);
    led_set_color(255, 255, 255);
    led_set_brightness(255);
    led_fade_out();
}