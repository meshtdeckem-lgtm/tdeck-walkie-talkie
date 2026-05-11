/**
 * lv_conf.h — LVGL 9.x configuration for T-Deck Plus
 * Place at project root (PIO will pick it up via -D LV_CONF_INCLUDE_SIMPLE)
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH                 16
#define LV_COLOR_16_SWAP               1     // ST7789 needs byte-swap on most TFT_eSPI configs
#define LV_USE_OS                      LV_OS_NONE   // bare-metal Arduino loop
#define LV_USE_STDLIB_MALLOC           LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING           LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF          LV_STDLIB_BUILTIN

// Memory: pull from PSRAM-friendly heap
#define LV_MEM_SIZE                    (64 * 1024)
#define LV_MEM_POOL_INCLUDE            <stdlib.h>
#define LV_USE_BUILTIN_MALLOC          1

// Display timing
#define LV_DEF_REFR_PERIOD             20
#define LV_TICK_CUSTOM                 1
#define LV_TICK_CUSTOM_INCLUDE         "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR   (millis())

// Input — keypad only (no touch on T-Deck Plus)
#define LV_USE_INDEV_KEYPAD            1
#define LV_USE_INDEV_ENCODER           1   // map trackball as encoder if desired

// Widgets we use
#define LV_USE_LABEL                   1
#define LV_USE_BAR                     1
#define LV_USE_SLIDER                  1
#define LV_USE_BUTTON                  1
#define LV_USE_DROPDOWN                1
#define LV_USE_ARC                     1
#define LV_USE_LINE                    1
#define LV_USE_CHART                   0
#define LV_USE_TABVIEW                 1
#define LV_USE_LIST                    1
#define LV_USE_SWITCH                  1
#define LV_USE_TEXTAREA                1
#define LV_USE_KEYBOARD                0  // we use the physical keyboard

// Themes
#define LV_USE_THEME_DEFAULT           1
#define LV_THEME_DEFAULT_DARK          1
#define LV_THEME_DEFAULT_GROW          0

// Fonts (Montserrat is LVGL default — keep small set to save flash)
#define LV_FONT_MONTSERRAT_12          1
#define LV_FONT_MONTSERRAT_14          1
#define LV_FONT_MONTSERRAT_16          1
#define LV_FONT_MONTSERRAT_24          1
#define LV_FONT_DEFAULT                &lv_font_montserrat_14

// Drawing
#define LV_DRAW_SW_COMPLEX             1
#define LV_USE_DRAW_SW                 1

// Logging
#define LV_USE_LOG                     0   // turn on for debugging

// File system / images — off (we use code-defined widgets only)
#define LV_USE_FS_STDIO                0
#define LV_USE_PNG                     0
#define LV_USE_BMP                     0

#endif // LV_CONF_H
