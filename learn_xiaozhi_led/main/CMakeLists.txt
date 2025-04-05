set(SOURCES "led/single_led.cc"
            "system_info.cc"
            "application.cc"
            "main.cc"
            )
set(INCLUDE_DIRS "." "led")

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    )