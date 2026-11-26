if(CONFIG_IR_MODE_RECEIVER)
    set(APP_SOURCE "receptor_ir.c")
elseif(CONFIG_IR_MODE_TRANSMITTER)
    set(APP_SOURCE "transmissor_ir.c")
endif()

idf_component_register(
    SRCS "${APP_SOURCE}"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_rmt
)