#Source Files Set
set(SOURCES "audio_codecs/audio_codec.cc"
            "audio_codecs/no_audio_codec.cc"
            "led/single_led.cc"
            "display/display.cc"
            "display/lcd_display.cc"
            "display/oled_display.cc"
            "system_info.cc"
            "application.cc"
            "settings.cc"
            "background_task.cc"
            "main.cc")

#Include Paths Set
set(INCLUDE_DIRS "." 
                 "display"
                 "audio_codecs")

# 添加板级公共文件
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
list(APPEND SOURCES ${BOARD_COMMON_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)    

# 设置 BOARD_TYPE 固定为 wifi-lcd 面包板
if(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI)
    set(BOARD_TYPE "bread-compact-wifi")
    list(REMOVE_ITEM SOURCES "display/lcd_display.cc")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD)
    set(BOARD_TYPE "bread-compact-wifi-lcd")
    list(REMOVE_ITEM SOURCES "display/oled_display.cc")
endif()

file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})

#Register All Components                 
idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE)

# 使用 target_compile_definitions 来定义 BOARD_TYPE, BOARD_NAME
# 如果 BOARD_NAME 为空，则使用 BOARD_TYPE
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    )

# 设置语言固定为中文简体
set(LANG_DIR "zh-CN")
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")

# 添加生成规则，${LANE_HEADER}文件由gen_lang.py调用输入文件${LANG_JSON}生成
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --input "${LANG_JSON}"
            --output "${LANG_HEADER}"
    DEPENDS
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# 强制建立生成依赖，确保在${LANG_HEADER}生成之后才能编译主工程
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)


