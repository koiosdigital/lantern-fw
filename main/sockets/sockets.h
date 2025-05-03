#pragma once

#include <stdint.h>
#include <stdlib.h>
#include "device-api.pb-c.h"

#define SOCKETS_URI "wss://device.api.koiosdigital.net/"

void sockets_init();
void sockets_disconnect();
void sockets_connect();

void notify_touch();
void send_device_api_message(Kd__DeviceAPIMessage* message);
void upload_coredump_task(void* pvParameter);