#pragma once

#include <stdint.h>
#include <stdlib.h>

#define SOCKETS_URI "wss://device.api.koiosdigital.net/lantern"

void sockets_init();
void sockets_disconnect();
void sockets_connect();

void notify_touch();
void upload_coredump(uint8_t* core_dump, size_t core_dump_len);
void attempt_coredump_upload();