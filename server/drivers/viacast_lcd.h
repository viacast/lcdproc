#ifndef VIACAST_LCD_H
#define VIACAST_LCD_H

#ifndef LCD_H
#include "lcd.h"
#endif

#define NO_DEVICE         ""
#define DEFAULT_DEVICE		"/dev/ttyACM0"
#define DEFAULT_FBDEV		"/dev/fb0"
#define DEFAULT_SPEED		B115200
#define DEFAULT_MAX_BATTERY	168
#define DEFAULT_MIN_BATTERY	124
#define DEFAULT_MIN_FONT	172
#define DEFAULT_ROTATE	0
#define DEFAULT_KEYPAD_ROTATE	0
#define DEFAULT_SIZE_LCDPROC		"20x4"
#define DEFAULT_ALPHA_BG  -0.4
#define DEFAULT_MARGIN_ALPHA  4
#define DEFAULT_HEIGHT_ICON  16
#define DEFAULT_V_SPACE_ICON 0
#define DEFAULT_H_SPACE_ICON 0

MODULE_EXPORT int  viacast_lcd_init (Driver *drvthis);
MODULE_EXPORT void viacast_lcd_close (Driver *drvthis);
MODULE_EXPORT int  viacast_lcd_width (Driver *drvthis);
MODULE_EXPORT int  viacast_lcd_height (Driver *drvthis);
MODULE_EXPORT void viacast_lcd_clear (Driver *drvthis);
MODULE_EXPORT void viacast_lcd_flush (Driver *drvthis);
MODULE_EXPORT void viacast_lcd_string (Driver *drvthis, int x, int y, const char string[]);
MODULE_EXPORT void viacast_lcd_chr (Driver *drvthis, int x, int y, char c);
MODULE_EXPORT void viacast_lcd_set_contrast (Driver *drvthis, int promille);
MODULE_EXPORT void viacast_lcd_set_rotate(Driver *drvthis, int rotate);
MODULE_EXPORT void viacast_lcd_backlight (Driver *drvthis, int on);
MODULE_EXPORT const char * viacast_lcd_get_info (Driver *drvthis);

MODULE_EXPORT const char * viacast_lcd_get_key(Driver *drvthis);
MODULE_EXPORT const char * viacast_lcd_get_pretty_name(Driver *drvthis);


#endif
