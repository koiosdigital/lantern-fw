#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_event.h"

#include "kd_common.h"
#include "sockets.h"

extern "C" void app_main(void)
{
    //event loop
    esp_event_loop_create_default();

    kd_common_set_provisioning_pop_token_format(ProvisioningPOPTokenFormat_t::NUMERIC_6);
    kd_common_init();

    sockets_init();

    vTaskSuspend(NULL);
}
