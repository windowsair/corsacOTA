# Use as component
set(COMPONENT_ADD_INCLUDEDIRS "./src")
set(COMPONENT_SRCS "./src/corsacOTA.c")
set(COMPONENT_REQUIRES app_update mbedtls freertos)

if(NOT IDF_TARGET STREQUAL "esp8266")
    list(APPEND COMPONENT_REQUIRES esp_hal_wdt esp_hal_timg)
endif()

register_component()

