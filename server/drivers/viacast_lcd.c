/** \file server/drivers/text.c
 * LCDd \c text driver for dump text mode terminals.
 * It displays the LCD screens, one below the other on the terminal,
 * and is this suitable for dump hard-copy terminals.
 */

/* Copyright (C) 1998-2004 The LCDproc Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>      
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <gfxprim/gfxprim.h>
#include <gfxprim/text/gp_text_style.h>
#include <gfxprim/text/gp_fonts.h>

#include "lcd.h"
#include "viacast_lcd.h"
#include "shared/report.h"

static char *directionKeyMap[4] = {"Down", "Left", "Up", "Right"};

/** private data for the \c viacast_lcd driver */
typedef struct text_private_data {
	char device[200];
  int fd;
	int fd_fbdev;
	int width;		/**< display width in characters */
	int height;		/**< display height in characters */
	char *framebuf_lcdproc;		/**< fram buffer  lcdproc*/
	char fbdev[200];
	int fbdev_bytes;
	int fbdev_data_size;
  char *framebuf_fbdev;		/**< fram buffer /dev/fbdev*/
  char *framebuf_fbdev_copy;		/**< fram buffer /dev/fbdev*/
	struct fb_var_screeninfo fb_info;
	int autorotate;
	int rotate;
	int keypad_rotate;
	
	gp_pixmap *pixmap;
	gp_pixel black_pixel;
	gp_pixel white_pixel;
	gp_text_style text_style;
} PrivateData;


/* Vars for the server core */
MODULE_EXPORT char *api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 0;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "viacast_lcd_";

// Internal functions
static void viacast_lcd_init_fbdev(Driver *drvthis);

/**
 * Initialize the driver.
 * \param drvthis  Pointer to driver structure.
 * \retval 0   Success.
 * \retval <0  Error.
 */
MODULE_EXPORT int
viacast_lcd_init(Driver *drvthis)
{
	struct termios portset;
	int tmp, w, h;
	int reboot = 0;
	int usb = 0;
	int speed = DEFAULT_SPEED;
	char size[200] = DEFAULT_SIZE_LCDPROC;

	PrivateData *p;

	/* Allocate and store private data */
	p = (PrivateData *) calloc(1, sizeof(PrivateData));
	if (p == NULL)
		return -1;
	if (drvthis->store_private_ptr(drvthis, p))
		return -1;

	/* Initialize the PrivateData structure */
	p->fd = -1;
	p->fd_fbdev = -1;
	// p->cellwidth = DEFAULT_CELL_WIDTH;
	// p->cellheight = DEFAULT_CELL_HEIGHT;
	// p->ccmode = standard;

	debug(RPT_INFO, "viacast_lcd: init(%p)", drvthis);

	/* Read config file */
	/* Which device should be used */
	strncpy(p->device, drvthis->config_get_string(drvthis->name, "Device", 0, DEFAULT_DEVICE), sizeof(p->device));
	p->device[sizeof(p->device)-1] = '\0';
	report(RPT_INFO, "%s: using Device %s", drvthis->name, p->device);


	/* Which fbdev should be used */
	strncpy(p->fbdev, drvthis->config_get_string(drvthis->name, "Fbdev", 0, DEFAULT_FBDEV), sizeof(p->fbdev));
	p->fbdev[sizeof(p->device)-1] = '\0';
	report(RPT_INFO, "%s: using fbdev %s", drvthis->name, p->fbdev);

	/* Which size */
	strncpy(size, drvthis->config_get_string(drvthis->name, "Size", 0, DEFAULT_SIZE_LCDPROC), sizeof(size));
	size[sizeof(size)-1] = '\0';
	if ((sscanf(size, "%dx%d", &w, &h) != 2)
	    || (w <= 0) || (w > LCD_MAX_WIDTH)
	    || (h <= 0) || (h > LCD_MAX_HEIGHT)) {
		report(RPT_WARNING, "%s: cannot read Size: %s; using default %s",
				drvthis->name, size, DEFAULT_SIZE_LCDPROC);
		sscanf(DEFAULT_SIZE_LCDPROC, "%dx%d", &w, &h);
	}
	p->width = w;
	p->height = h;

	// /* Which contrast */
	// tmp = drvthis->config_get_int(drvthis->name, "Contrast", 0, DEFAULT_CONTRAST);
	// if ((tmp < 0) || (tmp > 1000)) {
	// 	report(RPT_WARNING, "%s: Contrast must be between 0 and 1000; using default %d",
	// 			drvthis->name, DEFAULT_CONTRAST);
	// 	tmp = DEFAULT_CONTRAST;
	// }
	// p->contrast = tmp;

	// /* Which backlight brightness */
	// tmp = drvthis->config_get_int(drvthis->name, "Brightness", 0, DEFAULT_BRIGHTNESS);
	// if ((tmp < 0) || (tmp > 1000)) {
	// 	report(RPT_WARNING, "%s: Brightness must be between 0 and 1000; using default %d",
	// 			drvthis->name, DEFAULT_BRIGHTNESS);
	// 	tmp = DEFAULT_BRIGHTNESS;
	// }
	// p->brightness = tmp;

	// /* Which backlight-off "brightness" */
	// tmp = drvthis->config_get_int(drvthis->name, "OffBrightness", 0, DEFAULT_OFFBRIGHTNESS);
	// if ((tmp < 0) || (tmp > 1000)) {
	// 	report(RPT_WARNING, "%s: OffBrightness must be between 0 and 1000; using default %d",
	// 			drvthis->name, DEFAULT_OFFBRIGHTNESS);
	// 	tmp = DEFAULT_OFFBRIGHTNESS;
	// }
	// p->offbrightness = tmp;


	/* Which rotate ?*/
	tmp = drvthis->config_get_int(drvthis->name, "Rotate", 0, DEFAULT_ROTATE);
	if ((tmp > 3) || (tmp < 0)){
		report(RPT_WARNING, "%s: Rotate must be between 0 and 3; using default %d",
				drvthis->name, DEFAULT_ROTATE);
		tmp = DEFAULT_ROTATE;
	}
	p->rotate = tmp;

	/* Which keypad rotate ?*/
	tmp = drvthis->config_get_int(drvthis->name, "KeypadRotate", 0, DEFAULT_ROTATE);
	if ((tmp > 3) || (tmp < 0)){
		report(RPT_WARNING, "%s: keypadRotate must be between 0 and 3; using default %d",
				drvthis->name, DEFAULT_KEYPAD_ROTATE);
		tmp = DEFAULT_ROTATE;
	}
	p->keypad_rotate = tmp;

	/* Auto rotate? */
	p->autorotate = drvthis->config_get_bool(drvthis->name, "AutoRotate", 0, 0);

	/* Which speed */
	tmp = drvthis->config_get_int(drvthis->name, "Speed", 0, DEFAULT_SPEED);
	if (tmp == 1200) speed = B1200;
	else if (tmp == 2400) speed = B2400;
	else if (tmp == 9600) speed = B9600;
	else if (tmp == 19200) speed = B19200;
	else if (tmp == 115200) speed = B115200;
	else {
		report(RPT_WARNING, "%s: Speed must be 1200, 2400, 9600, 19200 or 115200; using default %d",
				drvthis->name, DEFAULT_SPEED);
		speed = DEFAULT_SPEED;
	}

	// /* New firmware version? */
	// p->newfirmware = drvthis->config_get_bool(drvthis->name, "NewFirmware", 0, 0);

	/* Reboot display? */
	reboot = drvthis->config_get_bool(drvthis->name, "Reboot", 0, 0);

	/* Am I USB or not? */
	usb = drvthis->config_get_bool(drvthis->name, "USB", 0, 0);

	/* Set up io port correctly, and open it... */
	debug(RPT_DEBUG, "viacast_lcd: Opening device: %s", p->device);
	p->fd = open(p->device, O_RDWR | O_NOCTTY | O_SYNC);
	if (p->fd == -1) {
		report(RPT_ERR, "%s: open(%s) failed (%s)",
				drvthis->name, p->device, strerror(errno));
		return -1;
	}

	tcgetattr(p->fd, &portset);

	/* We use RAW mode */
#ifdef HAVE_CFMAKERAW
	/* The easy way */
	cfmakeraw(&portset);
#else
	/* The hard way */
	portset.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
				| INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
	portset.c_oflag &= ~OPOST;
	portset.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
	portset.c_cflag &= ~( CSIZE | PARENB | CRTSCTS );
	portset.c_cflag |= CS8 | CREAD | CLOCAL ;
#endif
	portset.c_cc[VMIN] = 0;
	portset.c_cc[VTIME] = 0;

	/* Set port speed */
	cfsetospeed(&portset, speed);
	cfsetispeed(&portset, speed);

	/* Do it... */
	tcsetattr(p->fd, TCSANOW, &portset);

	/* make sure the frame buffer of lcdproc is there... */
	p->framebuf_lcdproc = malloc(p->width * p->height);
	if (p->framebuf_lcdproc == NULL) {
		report(RPT_ERR, "%s: unable to create framebuffer lcdproc", drvthis->name);
		return -1;
	}
	memset(p->framebuf_lcdproc, ' ', p->width * p->height);


	/* Information about fbdev */
	p->fd_fbdev = open(p->fbdev, O_RDONLY);
	if (p->fd_fbdev < 0){
		report(RPT_ERR, "%s: open(%s) failed (%s)",
				drvthis->name, p->fbdev, strerror(errno));
		return -1;
	}
	ioctl(p->fd_fbdev, FBIOGET_VSCREENINFO, &p->fb_info);
	p->fbdev_bytes = p->fb_info.bits_per_pixel / 8;
	p->fbdev_data_size = p->fb_info.xres * p->fb_info.yres * p->fbdev_bytes;
	
	p->framebuf_fbdev = malloc(p->fbdev_data_size);
	// /* make sure the frame buffer of fbdev is there... */
	// p->pixmap->pixels = malloc(p->fbdev_data_size);
	// if (p->pixmap->pixels == NULL) {
	// 	report(RPT_ERR, "%s: unable to create framebuffer fbdev", drvthis->name);
	// 	return -1;
	// }
	// memset(p->pixmap->pixels, ' ', p->width * p->height);


  p->pixmap = gp_pixmap_alloc(p->fb_info.xres, p->fb_info.yres, GP_PIXEL_RGB565);
	p->black_pixel = gp_rgb_to_pixmap_pixel(0x00, 0x00, 0x00, p->pixmap);
  p->white_pixel = gp_rgb_to_pixmap_pixel(0xff, 0xff, 0xff, p->pixmap);
  gp_text_style tmp_style = GP_DEFAULT_TEXT_STYLE;
	
	
	switch (p->rotate)
	{
	case (1):
		const gp_font_family* font_family = gp_font_family_lookup("tiny");
		tmp_style.font = gp_font_family_face_lookup(font_family, GP_FONT_MONO);
		gp_pixmap_rotate_cw(p->pixmap);
		break;
	case (2):
		gp_pixmap_rotate_cw(p->pixmap);
		gp_pixmap_rotate_cw(p->pixmap);
	case (3):
		const gp_font_family* font_family = gp_font_family_lookup("tiny");
		tmp_style.font = gp_font_family_face_lookup(font_family, GP_FONT_MONO);
		gp_pixmap_rotate_ccw(p->pixmap);
		break;
	
	default:
		break;
	}

	p->text_style = tmp_style;	



	// if (p->rotate == 1){
	// 	gp_pixmap_rotate_cw()
	// }

	report(RPT_INFO, "Infos about fbdev\nwidth:%d\nheight:%d\nbits_per_pixel:%d",
	 p->fb_info.xres, p->fb_info.yres, p->fb_info.bits_per_pixel);

	sleep (1);
	report(RPT_DEBUG, "%s: init() done", drvthis->name);

	return 0;
}

/**
 * Close the driver (do necessary clean-up).
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
viacast_lcd_close(Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	if (p != NULL) {
		if (p->fd >= 0)
			close(p->fd);

		if (p->fd_fbdev >= 0)
			close(p->fd_fbdev);

		if (p->framebuf_lcdproc)
			free(p->framebuf_lcdproc);
		p->framebuf_lcdproc = NULL;

		if (p->framebuf_fbdev)
			free(p->framebuf_fbdev);
		p->framebuf_fbdev = NULL;

		free(p);
	}
	drvthis->store_private_ptr(drvthis, NULL);
}

/**
 * Return the display width in characters.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of characters the display is wide.
 */
MODULE_EXPORT int
viacast_lcd_width(Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	return p->width;
}

/**
 * Return the display height in characters.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of characters the display is high.
 */
MODULE_EXPORT int
viacast_lcd_height(Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	return p->height;
}

/**
 * Clear the screen.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
viacast_lcd_clear (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	memset(p->framebuf_lcdproc, ' ', p->width * p->height);
}

/**
 * Flush data on screen to the display.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
viacast_lcd_flush (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;


	p->framebuf_fbdev = mmap (0, p->fbdev_data_size, PROT_READ, MAP_SHARED, p->fd_fbdev, (off_t)0);

	memcpy(p->pixmap->pixels, p->framebuf_fbdev, p->fbdev_data_size);

	gp_pixmap* res1 = gp_filter_resize_alloc(p->pixmap, gp_pixmap_w(p->pixmap), (gp_pixmap_w(p->pixmap) / 160) * 128, 0, NULL);
	gp_fill(p->pixmap, p->black_pixel);
	gp_blit_clipped(res1,0,0,gp_pixmap_w(res1),gp_pixmap_h(res1),p->pixmap,0,0);
	gp_pixmap_free(res1);		

	// gp_pixmap *subpixmap = gp_sub_pixmap_alloc(p->pixmap,0,0,20,4);
	// gp_coord x = 0;
	// int text_height = gp_text_height(&p->text_style);
	// gp_coord y =  gp_pixmap_h(p->pixmap) - (p->height * text_height);
	// char string [p->width];
	// int i = 0;
	// for (i = 0 ; i < p->height; i++){
	// 	strncpy(string, p->framebuf_lcdproc + (i* p->width), p->width);
	// 	gp_text(p->pixmap, &p->text_style, x, y, GP_ALIGN_RIGHT|GP_VALIGN_BELOW|GP_TEXT_BEARING, p->white_pixel, p->black_pixel, string);
	// 	y +=  text_height;
	// }

	int i  = write(p->fd, p->pixmap->pixels , p->fbdev_data_size);
	if (i != 40960)
		report(RPT_INFO, "Bytes writed %d",i);
}


/**
 * Print a string on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width, p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param string   String that gets written.
 */
MODULE_EXPORT void
viacast_lcd_string(Driver *drvthis, int x, int y, const char string[])
{
	PrivateData *p = drvthis->private_data;
	int i;

	/* Convert 1-based coords to 0-based... */
	x--;
	y--;

	if ((y < 0) || (y >= p->height))
		return;

	for (i = 0; (string[i] != '\0') && (x < p->width); i++, x++) {
		/* Check for buffer overflows... */
		if (x >= 0)
			p->framebuf_lcdproc[(y * p->width) + x] = string[i];
	}
}

/**
 * Get key from the device.
 * \param drvthis  Pointer to driver structure.
 * \return         String representation of the key;
 *                 \c NULL if nothing available / unmapped key.
 */
MODULE_EXPORT const char *
viacast_lcd_get_key(Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;
	char key[128]= {'\0'};
	read(p->fd, &key, 128);

	if (p->autorotate){
		if (key[0] & 0b00000001){
			p->rotate = 0;
		}
		if (key[0] & 0b00000010){
			p->rotate = 1;
		}
		if (key[0] & 0b00000100){
			p->rotate = 2;
		}
		if (key[0] & 0b0001000){
			p->rotate = 3;
		}
	} 

	switch (key[0]){
		case 'L':
			return directionKeyMap[(0 + p->rotate + p->keypad_rotate)%4];
		case 'U':
			return directionKeyMap[(1 + p->rotate + p->keypad_rotate)%4];
		case 'R':
			return directionKeyMap[(2 + p->rotate + p->keypad_rotate)%4];
		case 'D':
			return directionKeyMap[(3 + p->rotate + p->keypad_rotate)%4];
		case 'E':
			return "Enter";
		case 'C':
			return "Escape";
		default:
			report(RPT_INFO, "%s: Untreated key 0x%02X", drvthis->name, key);
			return NULL;
	}
}