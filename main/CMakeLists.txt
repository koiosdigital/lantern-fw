file(GLOB_RECURSE NESTED_SRC
       **.cpp
)

idf_component_register(
    SRCS ${NESTED_SRC}
    INCLUDE_DIRS "." "sockets" "led"
    REQUIRES esp_wifi heap json qrcode bootloader_support kd_common esp_http_client wifi_provisioning esp_driver_rmt kd-protobufs driver esp_app_format
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-missing-field-initializers" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=parentheses" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-write-strings" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DDEVICE_NAME_PREFIX=\"LANTERN\"" APPEND)