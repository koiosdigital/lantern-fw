#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "driver/touch_pad.h"

#include "kd_common.h"
#include "sockets.h"
#include "pinout.h"
#include "led.h"

static void tp_example_read_task(void* pvParameter)
{
    uint32_t touch_value;
    bool is_touched = false;

    while (1) {
        touch_pad_read_raw_data((touch_pad_t)TOUCH_PIN, &touch_value);    // read raw data.

        if (touch_value > 100000 && !is_touched) {
            ESP_LOGI("TOUCH", "TOUCH PIN");
            is_touched = true;
            led_set_effect(LED_CYCLIC);
            led_set_color(0, 255, 0);
            notify_touch();
        }

        if (touch_value < 100000 && is_touched) {
            ESP_LOGI("TOUCH", "RELEASE PIN");
            led_set_effect(LED_OFF);
            led_set_color(0, 0, 0);
            is_touched = false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main(void)
{
    //event loop
    esp_event_loop_create_default();

    led_init();

    kd_common_set_provisioning_pop_token_format(ProvisioningPOPTokenFormat_t::NUMERIC_6);
    kd_common_init();

    sockets_init();

    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config((touch_pad_t)TOUCH_PIN);

    touch_pad_set_measurement_interval(TOUCH_PAD_SLEEP_CYCLE_DEFAULT);
    touch_pad_set_charge_discharge_times(TOUCH_PAD_MEASURE_CYCLE_DEFAULT);
    touch_pad_set_voltage(TOUCH_PAD_HIGH_VOLTAGE_THRESHOLD, TOUCH_PAD_LOW_VOLTAGE_THRESHOLD, TOUCH_PAD_ATTEN_VOLTAGE_THRESHOLD);
    touch_pad_set_idle_channel_connect(TOUCH_PAD_IDLE_CH_CONNECT_DEFAULT);
    touch_pad_set_cnt_mode((touch_pad_t)TOUCH_PIN, TOUCH_PAD_SLOPE_DEFAULT, TOUCH_PAD_TIE_OPT_DEFAULT);

    /* Denoise setting at TouchSensor 0. */
    touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();

    /* Enable touch sensor clock. Work mode is "timer trigger". */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    xTaskCreate(&tp_example_read_task, "touch_pad_read_task", 4096, NULL, 5, NULL);
}
