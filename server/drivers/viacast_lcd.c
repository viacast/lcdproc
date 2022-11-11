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

#include <errno.h>
#include <fcntl.h>
#include <gfxprim/gfxprim.h>
#include <gfxprim/text/gp_fonts.h>
#include <gfxprim/text/gp_text_style.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "lcd.h"
#include "shared/report.h"
#include "timing.h"
#include "viacast_lcd.h"

static char *KeyMap[6] = {"Down", "Left", "Up", "Right", "Enter", "Escape"};

/** private data for the \c viacast_lcd driver */
typedef struct text_private_data {
  char device[200];
  int fd;
  int fd_fbdev;
  int width;              /**< display width in characters */
  int height;             /**< display height in characters */
  char *framebuf_lcdproc; /**< fram buffer  lcdproc*/
  char fbdev[200];
  int fbdev_bytes;
  int fbdev_data_size;
  char *framebuf_fbdev; /**< fram buffer /dev/fbdev*/
  struct fb_var_screeninfo fb_info;
  int autorotate;
  int rotate;
  int keypad_rotate;

  long timer;

  struct timeval *key_wait_time; /**< Time until key auto repeat */
  int key_repeat_delay;          /**< Time until first key repeat */
  int key_repeat_interval;       /**< Time between auto repeated keys */

  int pressed_index_key;
  int resize;
  int display_text;
  int hide_text;
  int secs_hide_text;
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
static void revestr(char *str1);

void revstr(char *str1)
{
  int i, len, temp;
  len = strlen(str1);

  for (i = 0; i < len / 2; i++) {
    temp = str1[i];
    str1[i] = str1[len - i - 1];
    str1[len - i - 1] = temp;
  }
}

/**
 * Initialize the driver.
 * \param drvthis  Pointer to driver structure.
 * \retval 0   Success.
 * \retval <0  Error.
 */
MODULE_EXPORT int viacast_lcd_init(Driver *drvthis)
{
  struct termios portset;
  int tmp, w, h;
  int reboot = 0;
  int usb = 0;
  int speed = DEFAULT_SPEED;
  char size[200] = DEFAULT_SIZE_LCDPROC;

  PrivateData *p;

  /* Allocate and store private data */
  p = (PrivateData *)calloc(1, sizeof(PrivateData));
  if (p == NULL)
    return -1;
  if (drvthis->store_private_ptr(drvthis, p))
    return -1;

  /* Initialize the PrivateData structure */
  p->fd = -1;
  p->fd_fbdev = -1;
  p->resize = 0;
  p->display_text = 1;
  p->hide_text = 1;
  p->timer = 0;

  // p->cellwidth = DEFAULT_CELL_WIDTH;
  // p->cellheight = DEFAULT_CELL_HEIGHT;
  // p->ccmode = standard;

  debug(RPT_INFO, "viacast_lcd: init(%p)", drvthis);

  /* Read config file */
  /* Which device should be used */
  strncpy(
      p->device,
      drvthis->config_get_string(drvthis->name, "Device", 0, DEFAULT_DEVICE),
      sizeof(p->device));
  p->device[sizeof(p->device) - 1] = '\0';
  report(RPT_INFO, "%s: using Device %s", drvthis->name, p->device);

  /* Which fbdev should be used */
  strncpy(p->fbdev,
          drvthis->config_get_string(drvthis->name, "Fbdev", 0, DEFAULT_FBDEV),
          sizeof(p->fbdev));
  p->fbdev[sizeof(p->device) - 1] = '\0';
  report(RPT_INFO, "%s: using fbdev %s", drvthis->name, p->fbdev);

  /* Which size */
  strncpy(size,
          drvthis->config_get_string(drvthis->name, "Size", 0,
                                     DEFAULT_SIZE_LCDPROC),
          sizeof(size));
  size[sizeof(size) - 1] = '\0';
  if ((sscanf(size, "%dx%d", &w, &h) != 2) || (w <= 0) || (w > LCD_MAX_WIDTH) ||
      (h <= 0) || (h > LCD_MAX_HEIGHT)) {
    report(RPT_WARNING, "%s: cannot read Size: %s; using default %s",
           drvthis->name, size, DEFAULT_SIZE_LCDPROC);
    sscanf(DEFAULT_SIZE_LCDPROC, "%dx%d", &w, &h);
  }
  p->width = w;
  p->height = h;

  /* Which rotate ?*/
  tmp = drvthis->config_get_int(drvthis->name, "Rotate", 0, DEFAULT_ROTATE);
  if ((tmp > 3) || (tmp < 0)) {
    report(RPT_WARNING, "%s: Rotate must be between 0 and 3; using default %d",
           drvthis->name, DEFAULT_ROTATE);
    tmp = DEFAULT_ROTATE;
  }
  if ((tmp == 1) || tmp == 3)
    p->resize = 1;
  p->rotate = tmp;

  /* Which keypad rotate ?*/
  tmp =
      drvthis->config_get_int(drvthis->name, "KeypadRotate", 0, DEFAULT_ROTATE);
  if ((tmp > 3) || (tmp < 0)) {
    report(RPT_WARNING,
           "%s: keypadRotate must be between 0 and 3; using default %d",
           drvthis->name, DEFAULT_KEYPAD_ROTATE);
    tmp = DEFAULT_ROTATE;
  }
  p->keypad_rotate = tmp;

  /* Auto rotate? */
  p->autorotate = drvthis->config_get_bool(drvthis->name, "AutoRotate", 0, 0);

  /* Hide text? */
  p->hide_text = drvthis->config_get_bool(drvthis->name, "HideText", 0, 1);

  /* Secs to hide text ?*/
  tmp = drvthis->config_get_int(drvthis->name, "SecondsHideText", 0, 60);
  if ((tmp > 120) || (tmp < 0)) {
    report(RPT_WARNING,
           "%s: Seconds to hide must be between 0 and 120; using default 60");
    tmp = 60;
  }
  p->secs_hide_text = tmp;

  /* Which speed */
  tmp = drvthis->config_get_int(drvthis->name, "Speed", 0, DEFAULT_SPEED);
  if (tmp == 1200)
    speed = B1200;
  else if (tmp == 2400)
    speed = B2400;
  else if (tmp == 9600)
    speed = B9600;
  else if (tmp == 19200)
    speed = B19200;
  else if (tmp == 115200)
    speed = B115200;
  else {
    report(
        RPT_WARNING,
        "%s: Speed must be 1200, 2400, 9600, 19200 or 115200; using default %d",
        drvthis->name, DEFAULT_SPEED);
    speed = DEFAULT_SPEED;
  }

  /* Set up io port correctly, and open it... */
  debug(RPT_DEBUG, "viacast_lcd: Opening device: %s", p->device);
  p->fd = open(p->device, O_RDWR | O_NOCTTY | O_SYNC);
  if (p->fd == -1) {
    report(RPT_ERR, "%s: open(%s) failed (%s)", drvthis->name, p->device,
           strerror(errno));
    return -1;
  }

  tcgetattr(p->fd, &portset);

  /* We use RAW mode */
#ifdef HAVE_CFMAKERAW
  /* The easy way */
  cfmakeraw(&portset);
#else
  /* The hard way */
  portset.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                       ICRNL | IXON | IXOFF | IXANY);
  portset.c_oflag &= ~OPOST;
  portset.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  portset.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
  portset.c_cflag |= CS8 | CREAD | CLOCAL;
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
  if (p->fd_fbdev < 0) {
    report(RPT_ERR, "%s: open(%s) failed (%s)", drvthis->name, p->fbdev,
           strerror(errno));
    return -1;
  }
  ioctl(p->fd_fbdev, FBIOGET_VSCREENINFO, &p->fb_info);
  p->fbdev_bytes = p->fb_info.bits_per_pixel / 8;
  p->fbdev_data_size = p->fb_info.xres * p->fb_info.yres * p->fbdev_bytes;
  p->framebuf_fbdev = malloc(p->fbdev_data_size);

  /* Information about pixmap */
  p->pixmap =
      gp_pixmap_alloc(p->fb_info.xres, p->fb_info.yres, GP_PIXEL_RGB565);
  p->black_pixel = gp_rgb_to_pixmap_pixel(0x00, 0x00, 0x00, p->pixmap);
  p->white_pixel = gp_rgb_to_pixmap_pixel(0xff, 0xff, 0xff, p->pixmap);
  gp_text_style tmp_style = GP_DEFAULT_TEXT_STYLE;

  /* Set rotations and font*/
  do {
    if (!p->resize)
      break;

    const gp_font_family *font_family;
    font_family = gp_font_family_lookup("tiny");
    tmp_style.font = gp_font_family_face_lookup(font_family, GP_FONT_MONO);

    if (p->rotate == 1)
      gp_pixmap_rotate_cw(p->pixmap);
    else if (p->rotate == 3)
      gp_pixmap_rotate_ccw(p->pixmap);
  } while (0);
  p->text_style = tmp_style;

  report(RPT_INFO, "Infos about fbdev\nwidth:%d\nheight:%d\nbits_per_pixel:%d",
         p->fb_info.xres, p->fb_info.yres, p->fb_info.bits_per_pixel);
  sleep(1);
  report(RPT_DEBUG, "%s: init() done", drvthis->name);

  /* Initialize delay */
  if ((p->key_wait_time = malloc(sizeof(struct timeval))) == NULL) {
    report(RPT_ERR, "%s: error allocating memory", drvthis->name);
    return -1;
  }
  timerclear(p->key_wait_time);

  /* Get key auto repeat delay */
  tmp = drvthis->config_get_int(drvthis->name, "KeyRepeatDelay", 0, 500);
  if (tmp < 0 || tmp > 3000) {
    report(RPT_WARNING,
           "%s: KeyRepeatDelay must be between 0-3000; using default %d",
           drvthis->name, 500);
    tmp = 500;
  }
  p->key_repeat_delay = tmp;

  /* Get key auto repeat interval */
  tmp = drvthis->config_get_int(drvthis->name, "KeyRepeatInterval", 0, 300);
  if (tmp < 0 || tmp > 3000) {
    report(RPT_WARNING,
           "%s: KeyRepeatInterval must be between 0-3000; using default %d",
           drvthis->name, 300);
    tmp = 700;
  }
  p->key_repeat_interval = tmp;

  return 0;
}

/**
 * Close the driver (do necessary clean-up).
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void viacast_lcd_close(Driver *drvthis)
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
MODULE_EXPORT int viacast_lcd_width(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;
  return p->width;
}

/**
 * Return the display height in characters.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of characters the display is high.
 */
MODULE_EXPORT int viacast_lcd_height(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;
  return p->height;
}

/**
 * Clear the screen.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void viacast_lcd_clear(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  memset(p->framebuf_lcdproc, ' ', p->width * p->height);
}

/**
 * Flush data on screen to the display.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void viacast_lcd_flush(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  p->framebuf_fbdev =
      mmap(0, p->fbdev_data_size, PROT_READ, MAP_SHARED, p->fd_fbdev, (off_t)0);
  memcpy(p->pixmap->pixels, p->framebuf_fbdev, p->fbdev_data_size);

  if (p->resize) {
    gp_pixmap *resized = gp_filter_resize_alloc(
        p->pixmap, gp_pixmap_w(p->pixmap),
        (gp_pixmap_w(p->pixmap) / gp_pixmap_h(p->pixmap)) *
            gp_pixmap_w(p->pixmap),
        GP_INTERP_NN, NULL);

    resized = gp_filter_rotate_180_alloc(resized, NULL);

    gp_fill(p->pixmap, p->black_pixel);
    gp_blit_clipped(resized, 0, 0, gp_pixmap_w(resized), gp_pixmap_h(resized),
                    p->pixmap, 0,
                    gp_pixmap_h(p->pixmap) - gp_pixmap_h(resized));
    gp_pixmap_free(resized);
  }

  gp_coord x = 0;
  gp_coord y;
  int text_height = gp_text_height(&p->text_style);
  char string[p->width];
  int i = 0;

  if (p->rotate == 1) {
    y = gp_pixmap_h(p->pixmap) - (p->height * text_height);
    for (i = 0; i < p->height; i++) {
      strncpy(string, p->framebuf_lcdproc + (i * p->width), p->width);
      revstr(string);
      gp_text(p->pixmap, &p->text_style, x, y,
              GP_ALIGN_RIGHT | GP_VALIGN_BELOW | GP_TEXT_BEARING,
              p->white_pixel, p->black_pixel, string);
      y += text_height;
    }
  }
  else if (p->rotate == 3) {
    y = (p->height * text_height);
    for (i = 0; i < p->height; i++) {
      strncpy(string, p->framebuf_lcdproc + (i * p->width), p->width);
      gp_text(p->pixmap, &p->text_style, x, y,
              GP_ALIGN_RIGHT | GP_VALIGN_BELOW | GP_TEXT_BEARING,
              p->white_pixel, p->black_pixel, string);
      y -= text_height;
    }
  }
  else if (!p->resize) {
    y = gp_pixmap_h(p->pixmap) - (p->height * text_height);

    if (p->display_text) {
      gp_pixmap *subpixmap = gp_sub_pixmap_alloc(
          p->pixmap, x, y - DEFAULT_MARGIN_ALPHA, gp_pixmap_w(p->pixmap),
          (p->height * text_height) + DEFAULT_MARGIN_ALPHA);
      gp_filter_brightness(subpixmap, subpixmap, -0.5, NULL);
      for (i = 0; i < p->height; i++) {
        strncpy(string, p->framebuf_lcdproc + (i * p->width), p->width);
        gp_text(p->pixmap, &p->text_style, x, y,
                GP_ALIGN_RIGHT | GP_VALIGN_BELOW | GP_TEXT_BEARING,
                p->white_pixel, p->black_pixel, string);
        y += text_height;
      }
    }
    if (p->rotate == 2)
      p->pixmap = gp_filter_rotate_180_alloc(p->pixmap, NULL);
  }

  write(p->fd, p->pixmap->pixels, p->fbdev_data_size);
}

/**
 * Print a string on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width,
 * p->height). \param drvthis  Pointer to driver structure. \param x Horizontal
 * character position (column). \param y        Vertical character position
 * (row). \param string   String that gets written.
 */
MODULE_EXPORT void viacast_lcd_string(Driver *drvthis, int x, int y,
                                      const char string[])
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
MODULE_EXPORT const char *viacast_lcd_get_key(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;
  char key = '\0';
  read(p->fd, &key, 1);
  int index = 0;
  int key_pressed = 0;
  struct timeval current_time, delay_time;

  if (p->autorotate) {
    if (key & 0b00000001) {
      p->rotate = 0;
    }
    if (key & 0b00000010) {
      p->rotate = 1;
    }
    if (key & 0b00000100) {
      p->rotate = 2;
    }
    if (key & 0b0001000) {
      p->rotate = 3;
    }
  }

  switch (key) {
  case 'L':
    index = (0 + p->rotate + p->keypad_rotate) % 4;
    key_pressed = 1;
    break;
  case 'U':
    index = (1 + p->rotate + p->keypad_rotate) % 4;
    key_pressed = 1;
    break;
  case 'R':
    index = (2 + p->rotate + p->keypad_rotate) % 4;
    key_pressed = 1;
    break;
  case 'D':
    index = (3 + p->rotate + p->keypad_rotate) % 4;
    key_pressed = 1;
    break;
  case 'E':
    index = 4;
    key_pressed = 1;
    break;
  case 'C':
    index = 5;
    key_pressed = 1;
    break;
  default:
    key_pressed = 0;
  }

  if (!timerisset(p->key_wait_time)) {
    gettimeofday(&current_time, NULL);
    /* Set first timer */
    delay_time.tv_sec = p->key_repeat_interval / 1000;
    delay_time.tv_usec = (p->key_repeat_interval % 1000) * 1000;
    timeradd(&current_time, &delay_time, p->key_wait_time);
  }

  if (!key_pressed) {
    return NULL;
  }

  gettimeofday(&current_time, NULL);
  /*
   * If a key has been pressed and it is not the same as in the previous
   * call to this function return that key string and start a timer. If
   * it is the same, check if the timer has passed. If not (or the timer
   * has been disabled) return no key string. Otherwise set the repeat
   * interval timer and return that key.
   */
  if (index == p->pressed_index_key) {
    if (timercmp(&current_time, p->key_wait_time, <)) {
      return NULL;
    }
  }

  /* 
   * Set new timer for debounce 
   */
  delay_time.tv_sec = p->key_repeat_interval / 1000;
  delay_time.tv_usec = (p->key_repeat_interval % 1000) * 1000;
  timeradd(&current_time, &delay_time, p->key_wait_time);

  report(RPT_DEBUG, "%s: New key pressed: %s", drvthis->name, KeyMap[index]);

  p->pressed_index_key = index;
  return (KeyMap[index]);
}