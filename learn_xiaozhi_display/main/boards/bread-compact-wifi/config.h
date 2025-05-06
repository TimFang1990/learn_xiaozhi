#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
//Platform: ESP32S3
#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
//Platform: ESP32C6
#define BUILTIN_LED_GPIO        GPIO_NUM_8 //Updated for ESP32C6-WROOM-1 (48->8), Build-in LED
#define BOOT_BUTTON_GPIO        GPIO_NUM_9 //Updated for ESP32C6-WROOM-1 (0->9), Build-in Boot Button
#define TOUCH_BUTTON_GPIO       GPIO_NUM_21 //Updated for ESP32C6-WROOM-1 (47->21), NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_20 //Updated for ESP32C6-WROOM-1 (40->20)
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_19 //Updated for ESP32C6-WROOM-1 (39->19)

#define DISPLAY_SDA_PIN GPIO_NUM_10 //Updated for ESP32C6-WROOM-1 (41->10)
#define DISPLAY_SCL_PIN GPIO_NUM_11 //Updated for ESP32C6-WROOM-1 (42->11)
#else
#error "不支持 ESP32C6 以及 ESP32S3 以外的平台"
#endif

#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#else
#error "未选择 OLED 屏幕类型"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

#endif // _BOARD_CONFIG_H_
