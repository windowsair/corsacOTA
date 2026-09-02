set(srcs "main.c" "../../src/corsacOTA.c")

if(IDF_TARGET STREQUAL "esp8266")
     list(APPEND srcs "connect/connect.c")
endif()

idf_component_register(SRCS "${srcs}")
