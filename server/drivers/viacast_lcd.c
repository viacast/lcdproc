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
#include <gfxprim/loaders/gp_loaders.h>
#include <gfxprim/text/gp_fonts.h>
#include <gfxprim/text/gp_text_style.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "lcd.h"
#include "lcd_lib.h"
#include "shared/report.h"
#include "timing.h"
#include "viacast_lcd.h"
#include "viacast_lcd_utils.h"

#define ValidX(x)                                                              \
  if ((x) > p->width) {                                                        \
    (x) = p->width;                                                            \
  }                                                                            \
  else                                                                         \
    (x) = (x) < 1 ? 1 : (x);

#define ValidY(y)                                                              \
  if ((y) > p->height) {                                                       \
    (y) = p->height;                                                           \
  }                                                                            \
  else                                                                         \
    (y) = (y) < 1 ? 1 : (y);

#define MAX_DEVICES 4
#define MAX_ICONS 4

static char *KeyMap[6] = {"Down", "Left", "Up", "Right", "Enter", "Escape"};

/** private data for the \c viacast_lcd driver */
typedef struct text_private_data {
  char device[MAX_DEVICES][200];
  int fd[MAX_DEVICES];
  int bytes_wrote[MAX_DEVICES];
  int has_device;
  int speed;
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

  struct dirent **icons_list;
  int reload_icons;
  int n_icons_l;
  int n_icons_l2;
  int n_icons_r;
  int always_status_bar;
  int always_text_bar;
  int status_bar;

  struct timeval *key_wait_time;     /**< Time until key auto repeat */
  struct timeval *display_wait_time; /**< Time until key auto repeat */
  int key_repeat_delay;              /**< Time until first key repeat */
  int key_repeat_interval;           /**< Time between auto repeated keys */

  int pressed_index_key;
  int resize;
  int display_text;
  int hide_text;
  int secs_hide_text;
  gp_pixmap *pixmap;
  gp_pixmap **icon_l;
  gp_pixmap **icon_l2;
  gp_pixmap **icon_r;
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
static int viacast_lcd_setup_device(Driver *drvthis, int index);
void viacast_lcd_setup_gfxprim(Driver *drvthis);
int reload_icons(Driver *drvthis);
void destroy_icons(Driver *drvthis);
void draw_icons_1(Driver *drvthis);
void draw_icons_2(Driver *drvthis);
void draw_icons_3(Driver *drvthis);
void sighandler(const int signal, void *ptr);
static int is_valid_fd(int fd);
static void revestr(char *str1);

void sighandler(const int signal, void *ptr)
{
  static PrivateData *p = NULL;

 

  if (p == NULL) {
    p = ptr;
    return;
  }
  if (signal == SIGRTMIN) {
    p->reload_icons = 1;
    return;
  }
  if (signal == (SIGRTMIN + 1)) {
    struct timeval current_time, delay_time;

    gettimeofday(&current_time, NULL);

    // Set new timer for hide text
    delay_time.tv_sec = p->secs_hide_text;
    delay_time.tv_usec = 0;
    timeradd(&current_time, &delay_time, p->display_wait_time);
    p->display_text = 1;
    p->status_bar = 1;
    return;
  }
}

int viacast_lcd_setup_device(Driver *drvthis, int index)
{
  PrivateData *p = drvthis->private_data;
  struct termios portset;

  /* Set up io port correctly, and open it... */
  debug(RPT_DEBUG, "viacast_lcd: Opening device: %s", p->device[index]);

  p->fd[index] = open(p->device[index], O_RDWR | O_NOCTTY | O_SYNC);
  if (p->fd[index] == -1) {
    report(RPT_ERR, "%s: open(%s) failed (%s)", drvthis->name, p->device[index],
           strerror(errno));
    return -1;
  }

  tcgetattr(p->fd[index], &portset);

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
  cfsetospeed(&portset, p->speed);
  cfsetispeed(&portset, p->speed);

  /* Do it... */
  tcsetattr(p->fd[index], TCSANOW, &portset);
  return 0;
}

void viacast_lcd_setup_gfxprim(Driver *drvthis)
{

  PrivateData *p = drvthis->private_data;

  gp_pixmap_set_rotation(p->pixmap, 0, 0, 0);
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
}

int reload_icons(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  destroy_icons(drvthis);

  char fullpath[1024];
  int n = 0;
  int i = 0;

  /* Left 1 */

  const char *directory_scanl = "/tmp/status_bar/left1";
  n = scandir(directory_scanl, &p->icons_list, filter, alphasort);

  do {
    if (n <= 0) {
      p->n_icons_l = n;
      break;
    }

    p->n_icons_l = n;
    p->icon_l = (gp_pixmap **)malloc(sizeof(gp_pixmap *) * n);

    for (i = 0; i < n; i++) {
      sprintf(fullpath, "%s/%s", directory_scanl, p->icons_list[i]->d_name);
      p->icon_l[i] = gp_load_png(fullpath, NULL);
    }
  } while (0);

  /* Left 2 */
  const char *directory_scanl2 = "/tmp/status_bar/left2";
  n = scandir(directory_scanl2, &p->icons_list, filter, alphasort);

  do {
    if (n <= 0) {
      p->n_icons_l2 = n;
      break;
    }

    p->n_icons_l2 = n;
    p->icon_l2 = (gp_pixmap **)malloc(sizeof(gp_pixmap *) * n);

    for (i = 0; i < n; i++) {
      sprintf(fullpath, "%s/%s", directory_scanl2, p->icons_list[i]->d_name);
      p->icon_l2[i] = gp_load_png(fullpath, NULL);
    }
  } while (0);

  /* Right */

  const char *directory_scanr = "/tmp/status_bar/right";
  n = scandir(directory_scanr, &p->icons_list, filter, alphasort);

  do {
    if (n <= 0) {
      p->n_icons_r = n;
      break;
    }

    p->n_icons_r = n;
    p->icon_r = (gp_pixmap **)malloc(sizeof(gp_pixmap *) * n);

    for (i = 0; i < n; i++) {
      sprintf(fullpath, "%s/%s", directory_scanr, p->icons_list[i]->d_name);
      p->icon_r[i] = gp_load_png(fullpath, NULL);
    }
  }

  while (0);
}

void destroy_icons(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  int i = 0;

  for (i = 0; i < p->n_icons_l; i++) {
    if (p->icon_l[i])
      gp_pixmap_free(p->icon_l[i]);
  }

  if (p->icon_l)
    free(p->icon_l);

  p->icon_l = NULL;

  for (i = 0; i < p->n_icons_l2; i++) {
    if (p->icon_l2[i])
      gp_pixmap_free(p->icon_l2[i]);
  }

  if (p->icon_l2)
    free(p->icon_l2);

  p->icon_l2 = NULL;

  for (i = 0; i < p->n_icons_r; i++) {
    if (p->icon_r[i])
      gp_pixmap_free(p->icon_r[i]);
  }

  if (p->icon_r)
    free(p->icon_r);

  p->icon_r = NULL;
}

void draw_icons_1(Driver *drvthis)
{

  PrivateData *p = drvthis->private_data;
  int text_height = gp_text_height(&p->text_style);

  if ((!p->icon_l) && (!p->icon_r) && (!p->icon_l2))
    return;

  int i = 0;
  gp_coord height_status_bar = DEFAULT_HEIGHT_ICON + (2 * DEFAULT_V_SPACE_ICON);

  gp_coord coordx = 0;
  gp_coord coordy = gp_pixmap_h(p->pixmap) - height_status_bar;

  gp_pixmap *temp_icon = NULL;

  gp_coord y_status_bar = coordy;
  gp_coord x_status_bar = 0;

  gp_coord x_status_bar_back = 0;
  gp_coord y_status_bar_back = 0;

  gp_coord x_available = gp_pixmap_w(p->pixmap);
  gp_coord x_width = 0;

  int need_create_status_bar = 1;

  /* Right */
  for (i = 0; i < p->n_icons_r; i++) {

    if (!p->icon_r[i])
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    x_width = gp_pixmap_w(p->icon_r[i]) + DEFAULT_H_SPACE_ICON;

    if (x_width > x_available)
      continue;

    temp_icon = gp_filter_rotate_180_alloc(p->icon_r[i], NULL);
    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);

    coordx += x_width;
    x_available -= x_width;
  }

  /* Left1 */
  coordx = gp_pixmap_w(p->pixmap);
  for (i = 0; i < p->n_icons_l; i++) {

    if (!p->icon_l[i])
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    x_width = gp_pixmap_w(p->icon_l[i]) + DEFAULT_H_SPACE_ICON;

    if (x_width > x_available)
      continue;

    coordx -= x_width;
    x_available -= x_width;

    temp_icon = gp_filter_rotate_180_alloc(p->icon_l[i], NULL);

    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);
  }

  /* Left2 */
  x_status_bar_back += height_status_bar;
  y_status_bar_back = 0;
  coordx = gp_pixmap_w(p->pixmap);
  coordy -= height_status_bar;
  x_available = gp_pixmap_w(p->pixmap);
  need_create_status_bar = 1;

  for (i = 0; i < p->n_icons_l2; i++) {

    if (!p->icon_l2[i])
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    x_width = gp_pixmap_w(p->icon_l2[i]) + DEFAULT_H_SPACE_ICON;

    if (x_width > x_available)
      continue;

    coordx -= x_width;
    x_available -= x_width;

    temp_icon = gp_filter_rotate_180_alloc(p->icon_l2[i], NULL);

    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);
  }

  if (temp_icon)
    gp_pixmap_free(temp_icon);
}

void draw_icons_3(Driver *drvthis)
{

  PrivateData *p = drvthis->private_data;
  int text_height = gp_text_height(&p->text_style);

  if ((!p->icon_l) && (!p->icon_r))
    return;

  int i = 0;
  gp_coord height_status_bar = DEFAULT_HEIGHT_ICON + (2 * DEFAULT_V_SPACE_ICON);
  gp_coord coordx = 0;
  gp_coord coordy = gp_pixmap_h(p->pixmap) - height_status_bar;
  gp_pixmap *temp_icon = NULL;

  gp_coord x_status_bar_back = gp_pixmap_w(p->pixmap) + height_status_bar;
  gp_coord y_status_bar_back = 0;

  gp_coord x_available = gp_pixmap_w(p->pixmap);
  gp_coord x_width = 0;

  int need_create_status_bar = 1;

  /* Right */
  for (i = 0; i < p->n_icons_r; i++) {

    if (!p->icon_r[i])
      continue;

    x_width = gp_pixmap_w(p->icon_r[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    temp_icon = gp_filter_rotate_180_alloc(p->icon_r[i], NULL);
    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);

    coordx += x_width;
    x_available -= x_width;
  }

  /* Left 1 */
  coordx = gp_pixmap_w(p->pixmap);
  for (i = 0; i < p->n_icons_l; i++) {

    if (!p->icon_l[i])
      continue;

    x_width = gp_pixmap_w(p->icon_l[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    coordx -= x_width;
    x_available -= x_width;

    temp_icon = gp_filter_rotate_180_alloc(p->icon_l[i], NULL);
    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);
  }

  /* Left 2 */
  coordx = gp_pixmap_w(p->pixmap);
  coordy -= height_status_bar;
  x_available = gp_pixmap_w(p->pixmap);
  x_status_bar_back -= height_status_bar;
  need_create_status_bar = 1;
  for (i = 0; i < p->n_icons_l2; i++) {

    if (!p->icon_l2[i])
      continue;

    x_width = gp_pixmap_w(p->icon_l2[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar_back, y_status_bar_back,
                              height_status_bar, gp_pixmap_w(p->pixmap),
                              p->pixmap, x_status_bar_back, y_status_bar_back,
                              DEFAULT_ALPHA_BG, NULL);
      need_create_status_bar = 0;
    }

    coordx -= x_width;
    x_available -= x_width;

    temp_icon = gp_filter_rotate_180_alloc(p->icon_l2[i], NULL);
    gp_blit_clipped(temp_icon, 0, 0, gp_pixmap_w(temp_icon),
                    gp_pixmap_h(temp_icon), p->pixmap, coordx, coordy);
  }

  if (temp_icon)
    gp_pixmap_free(temp_icon);
}

void draw_icons_2(Driver *drvthis)
{

  PrivateData *p = drvthis->private_data;

  if ((!p->icon_l) && (!p->icon_r) && (!p->icon_l2))
    return;

  int i = 0;
  gp_coord coordx = gp_pixmap_w(p->pixmap) - DEFAULT_H_SPACE_ICON;
  gp_coord coordy = 0;

  gp_coord height_status_bar = DEFAULT_HEIGHT_ICON + (2 * DEFAULT_V_SPACE_ICON);
  gp_coord y_status_bar = 0;
  gp_coord x_status_bar = 0;

  gp_coord x_available = gp_pixmap_w(p->pixmap);
  gp_coord x_width = 0;

  int need_create_status_bar = 1;
  for (i = 0; i < p->n_icons_r; i++) {

    if (!p->icon_r[i])
      continue;

    x_width = gp_pixmap_w(p->icon_r[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar, y_status_bar,
                              gp_pixmap_w(p->pixmap), height_status_bar,
                              p->pixmap, x_status_bar, y_status_bar,
                              DEFAULT_ALPHA_BG, NULL);

      need_create_status_bar = 0;
    }

    coordx -= x_width;
    x_available -= x_width;
    gp_blit_clipped(p->icon_r[i], 0, 0, gp_pixmap_w(p->icon_r[i]),
                    gp_pixmap_h(p->icon_r[i]), p->pixmap, coordx, coordy);
  }

  /* Left 1 */
  coordx = DEFAULT_H_SPACE_ICON;
  for (i = 0; i < p->n_icons_l; i++) {

    if (!p->icon_l[i])
      continue;

    x_width = gp_pixmap_w(p->icon_l[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar, y_status_bar,
                              gp_pixmap_w(p->pixmap), height_status_bar,
                              p->pixmap, x_status_bar, y_status_bar,
                              DEFAULT_ALPHA_BG, NULL);

      need_create_status_bar = 0;
    }

    gp_blit_clipped(p->icon_l[i], 0, 0, gp_pixmap_w(p->icon_l[i]),
                    gp_pixmap_h(p->icon_l[i]), p->pixmap, coordx, coordy);
    x_available -= x_width;
    coordx += x_width;
  }

  /* Left 2 */
  coordx = DEFAULT_H_SPACE_ICON;
  need_create_status_bar = 1;
  y_status_bar += height_status_bar;
  coordy += y_status_bar;
  x_available = gp_pixmap_w(p->pixmap);

  for (i = 0; i < p->n_icons_l2; i++) {

    if (!p->icon_l2[i])
      continue;

    x_width = gp_pixmap_w(p->icon_l2[i]) + DEFAULT_H_SPACE_ICON;
    if (x_width > x_available)
      continue;

    if (need_create_status_bar) {
      gp_filter_brightness_ex(p->pixmap, x_status_bar, y_status_bar,
                              gp_pixmap_w(p->pixmap), height_status_bar,
                              p->pixmap, x_status_bar, y_status_bar,
                              DEFAULT_ALPHA_BG, NULL);

      need_create_status_bar = 0;
    }

    gp_blit_clipped(p->icon_l2[i], 0, 0, gp_pixmap_w(p->icon_l2[i]),
                    gp_pixmap_h(p->icon_l2[i]), p->pixmap, coordx, coordy);
    x_available -= x_width;
    coordx += x_width;
  }
}

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
  int tmp, w, h;
  char size[200] = DEFAULT_SIZE_LCDPROC;

  PrivateData *p;

  /* Allocate and store private data */
  p = (PrivateData *)calloc(1, sizeof(PrivateData));
  if (p == NULL)
    return -1;
  if (drvthis->store_private_ptr(drvthis, p))
    return -1;

  /* Initialize the PrivateData structure */
  int i = 0;

  for (i = 0; i < MAX_DEVICES; i++)
    p->fd[i] = -1;
  p->fd_fbdev = -1;
  p->has_device = 0;
  p->resize = 0;
  p->display_text = 1;
  p->hide_text = 1;
  p->timer = 0;
  p->speed = DEFAULT_SPEED;
  p->status_bar = 1;

  debug(RPT_INFO, "viacast_lcd: init(%p)", drvthis);

  /* Read config file */
  /* Which device should be used */

  for (i = 0; i < MAX_DEVICES; i++) {
    strncpy(p->device[i],
            drvthis->config_get_string(drvthis->name, "Device", i, NO_DEVICE),
            sizeof(p->device[i]));
    if (strcmp(p->device[i], NO_DEVICE) == 0) {
      continue;
    }
    p->device[i][sizeof(p->device) - 1] = '\0';
    report(RPT_INFO, "%s: using Device %s", drvthis->name, p->device[i]);
  }

  /* Which fbdev should be used */
  strncpy(p->fbdev,
          drvthis->config_get_string(drvthis->name, "Fbdev", 0, DEFAULT_FBDEV),
          sizeof(p->fbdev));
  p->fbdev[sizeof(p->fbdev) - 1] = '\0';
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
           "%s: Seconds to hide must be between 0 and 120; using default 60",
           drvthis->name);
    tmp = 60;
  }
  p->secs_hide_text = tmp;

  /* Always status bar ?*/
  tmp = drvthis->config_get_bool(drvthis->name, "AlwaysStatusBar", 0, 0);
  if ((tmp > 1) || (tmp < 0)) {
    report(RPT_WARNING, "%s: using default value 0", drvthis->name);
    tmp = 0;
  }
  p->always_status_bar = tmp;

  /* Always text bar ?*/
  tmp = drvthis->config_get_bool(drvthis->name, "AlwaysTextBar", 0, 0);
  if ((tmp > 1) || (tmp < 0)) {
    report(RPT_WARNING, "%s: using default value 0", drvthis->name);
    tmp = 0;
  }
  p->always_text_bar = tmp;

  /* Which speed */
  tmp = drvthis->config_get_int(drvthis->name, "Speed", 0, DEFAULT_SPEED);
  if (tmp == 1200)
    p->speed = B1200;
  else if (tmp == 2400)
    p->speed = B2400;
  else if (tmp == 9600)
    p->speed = B9600;
  else if (tmp == 19200)
    p->speed = B19200;
  else if (tmp == 115200)
    p->speed = B115200;
  else {
    report(
        RPT_WARNING,
        "%s: Speed must be 1200, 2400, 9600, 19200 or 115200; using default %d",
        drvthis->name, DEFAULT_SPEED);
    p->speed = DEFAULT_SPEED;
  }

  int n_loaded_devices = 0;
  for (i = 0; i < MAX_DEVICES; i++) {
    if (viacast_lcd_setup_device(drvthis, i) == 0) {
      p->has_device |= 1 << i;
      n_loaded_devices++;
    }
  }

  if (n_loaded_devices == 0)
    return -1;

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

  /* Information about pixmap */
  p->pixmap =
      gp_pixmap_alloc(p->fb_info.xres, p->fb_info.yres, GP_PIXEL_RGB565);

  p->black_pixel = gp_rgb_to_pixmap_pixel(0x00, 0x00, 0x00, p->pixmap);
  p->white_pixel = gp_rgb_to_pixmap_pixel(0xff, 0xff, 0xff, p->pixmap);

  viacast_lcd_setup_gfxprim(drvthis);

  report(RPT_INFO, "Infos about fbdev\nwidth:%d\nheight:%d\nbits_per_pixel:%d",
         p->fb_info.xres, p->fb_info.yres, p->fb_info.bits_per_pixel);

  /* Initialize delay */
  if ((p->key_wait_time = malloc(sizeof(struct timeval))) == NULL) {
    report(RPT_ERR, "%s: error allocating memory", drvthis->name);
    return -1;
  }
  timerclear(p->key_wait_time);

  /* Initialize display time */
  if ((p->display_wait_time = malloc(sizeof(struct timeval))) == NULL) {
    report(RPT_ERR, "%s: error allocating memory", drvthis->name);
    return -1;
  }
  timerclear(p->display_wait_time);

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
    tmp = 300;
  }
  p->key_repeat_interval = tmp;

  p->framebuf_fbdev =
      mmap(0, p->fbdev_data_size, PROT_READ, MAP_SHARED, p->fd_fbdev, (off_t)0);

  if (p->framebuf_fbdev == MAP_FAILED) {
    perror("Mmap:");
    return -1;
  }

  /*Config iontify*/
  p->reload_icons = 1;

  signal(SIGRTMIN, sighandler);
  signal(SIGRTMIN+1, sighandler);
  sighandler(SIGRTMAX, drvthis->private_data);

  report(RPT_INFO, "%s: init() done", drvthis->name);
  sleep(1);

  return 0;
}

/**
 * Close the driver (do necessary clean-up).
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void viacast_lcd_close(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  report(RPT_DEBUG, "%s: Close", drvthis->name);

  destroy_icons(drvthis);

  if (p != NULL) {
    int i = 0;
    for (i = 0; i < MAX_DEVICES; i++)
      if (p->fd[i] >= 0)
        close(p->fd[i]);

    if (p->fd_fbdev >= 0)
      close(p->fd_fbdev);

    if (p->framebuf_lcdproc)
      free(p->framebuf_lcdproc);
    p->framebuf_lcdproc = NULL;

    if (p->pixmap)
      free(p->pixmap);
    p->pixmap = NULL;

    munmap(p->framebuf_fbdev, p->fbdev_data_size);

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
  // memset(p->framebuf_fbdev, ' ' , p->fbdev_data_size);

  int i = 0;
  for (i = 0; i < MAX_DEVICES; i++) {
    if (!(p->has_device & 1 << i))
      continue;
    if (p->bytes_wrote[i] < 0)
      if (viacast_lcd_setup_device(drvthis, i) < 0)
        p->has_device &= 0 << i;
    p->bytes_wrote[i] = 0;
  }
}

/**
 * Flush data on screen to the display.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void viacast_lcd_flush(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  if (p->reload_icons) {
    reload_icons(drvthis);
    p->reload_icons = 0;
  }

  memcpy(p->pixmap->pixels, p->framebuf_fbdev, p->fbdev_data_size);

  if (p->resize) {
    gp_pixmap *resized = gp_filter_resize_alloc(
        p->pixmap, gp_pixmap_w(p->pixmap),
        (gp_pixmap_w(p->pixmap) / gp_pixmap_h(p->pixmap)) *
            gp_pixmap_w(p->pixmap),
        GP_INTERP_NN, NULL);

    resized = gp_filter_rotate_180_alloc(resized, NULL);

    gp_fill(p->pixmap, p->black_pixel);
    gp_blit_clipped(
        resized, 0, 0, gp_pixmap_w(resized), gp_pixmap_h(resized), p->pixmap, 0,
        gp_pixmap_h(p->pixmap) - gp_pixmap_h(resized) - DEFAULT_HEIGHT_ICON);
    gp_pixmap_free(resized);
  }

  gp_coord x = 0;
  gp_coord y;
  int text_height = gp_text_height(&p->text_style);
  char string[p->width];
  int i = 0;

  if (p->rotate == 1) {

    draw_icons_1(drvthis);

    x = gp_pixmap_w(p->pixmap);
    x -= (p->text_style.font->max_glyph_width / 2);
    y = gp_pixmap_h(p->pixmap) - (p->height * text_height);
    for (i = 0; i < p->height; i++) {
      strncpy(string, p->framebuf_lcdproc + (i * p->width), p->width);
      revstr(string);
      gp_text(p->pixmap, &p->text_style, x, y,
              GP_ALIGN_LEFT | GP_VALIGN_BELOW | GP_TEXT_BEARING, p->white_pixel,
              p->black_pixel, string);
      y += text_height;
    }
  }
  else if (p->rotate == 3) {

    draw_icons_3(drvthis);

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

    if (p->status_bar) {
      draw_icons_2(drvthis);
    }

    if (p->display_text) {
      gp_filter_brightness_ex(
          p->pixmap, x, y - DEFAULT_MARGIN_ALPHA, gp_pixmap_w(p->pixmap),
          (p->height * text_height) + DEFAULT_MARGIN_ALPHA, p->pixmap, x,
          y - DEFAULT_MARGIN_ALPHA, DEFAULT_ALPHA_BG, NULL);

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

  for (i = 0; i < MAX_DEVICES; i++) {

    if (p->fd[i] <= 0)
      continue;

    while (p->bytes_wrote[i] < p->fbdev_data_size) {

      int temp_bytes = write(p->fd[i], p->pixmap->pixels + p->bytes_wrote[i],
                             p->fbdev_data_size - p->bytes_wrote[i]);

      if (temp_bytes < 0) {
        p->bytes_wrote[i] = temp_bytes;
        break;
      }

      p->bytes_wrote[i] += temp_bytes;
    }
  }
}

/**
 * Print a string on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width,
 * p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param string   String that gets written.
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
  char key[MAX_DEVICES][128] = {{0}};
  int i = 0;
  int index = 0;
  int key_pressed = 0;
  struct timeval current_time, delay_time;

  gettimeofday(&current_time, NULL);
  if (!timerisset(p->key_wait_time)) {
    /* Set first timer */
    delay_time.tv_sec = p->key_repeat_interval / 1000;
    delay_time.tv_usec = (p->key_repeat_interval % 1000) * 1000;
    timeradd(&current_time, &delay_time, p->key_wait_time);
  }

  if (!timerisset(p->display_wait_time)) {
    /* Set first timer for display */
    delay_time.tv_sec = p->secs_hide_text;
    delay_time.tv_usec = 0;
    timeradd(&current_time, &delay_time, p->display_wait_time);
  }

  for (i = 0; i < MAX_DEVICES; i++) {

    if (!(p->has_device & 1 << i)) {
      continue;
    }

    int r = read(p->fd[i], &key[i], 128);
    if (r <= 0)
      continue;

    switch (key[i][0]) {
    case 'L':
      index = (0 + p->rotate + p->keypad_rotate) % 4;
      key_pressed |= 1 << i;
      break;
    case 'U':
      index = (1 + p->rotate + p->keypad_rotate) % 4;
      key_pressed |= 1 << i;
      break;
    case 'R':
      index = (2 + p->rotate + p->keypad_rotate) % 4;
      key_pressed |= 1 << i;
      break;
    case 'D':
      index = (3 + p->rotate + p->keypad_rotate) % 4;
      key_pressed |= 1 << i;
      break;
    case 'E':
      index = 4;
      key_pressed |= 1 << i;
      break;
    case 'C':
      index = 5;
      key_pressed |= 1 << i;
      break;
    default:
      break;
    }
  }

  if (timercmp(&current_time, p->key_wait_time, <)) {
    return NULL;
  }

  if (!key_pressed) {
    do {

      if (p->always_text_bar) {
        p->display_text = 1;
        break;
      }

      if (!p->display_text)
        break;

      // lcd rotate is always displayed
      if (p->resize)
        break;

      if (timercmp(&current_time, p->display_wait_time, <))
        break;

      p->display_text = 0;
      p->status_bar = p->always_status_bar ? 1 : 0;
    } while (0);
    return NULL;
  }

  // From here key pressed
  // Set new timer for debounce
  delay_time.tv_sec = p->key_repeat_interval / 1000;
  delay_time.tv_usec = (p->key_repeat_interval % 1000) * 1000;
  timeradd(&current_time, &delay_time, p->key_wait_time);

  // Set new timer for hide text
  delay_time.tv_sec = p->secs_hide_text;
  delay_time.tv_usec = 0;
  timeradd(&current_time, &delay_time, p->display_wait_time);

  if (!p->display_text) {
    p->display_text = 1;
    p->status_bar = 1;
    return NULL;
  }

  p->pressed_index_key = index;
  return (KeyMap[index]);
}

/**
 * Print a character on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width,
 * p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param c        Character that gets written.
 */
MODULE_EXPORT void viacast_lcd_chr(Driver *drvthis, int x, int y, char c)
{
  PrivateData *p = drvthis->private_data;

  int offset;

  ValidX(x);
  ValidY(y);

  y--;
  x--;

  offset = (y * p->width) + x;
  p->framebuf_lcdproc[offset] = c;

  report(RPT_DEBUG, "%s: writing icon initial position to position (%d,%d)",
         __FUNCTION__, offset, y);
}

/**
 * Place an icon on the screen.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param icon     synbolic value representing the icon.
 * \retval 0       Icon has been successfully defined/written.
 * \retval <0      Server core shall define/write the icon.
 */
MODULE_EXPORT int viacast_lcd_icon(Driver *drvthis, int x, int y, int icon)
{
  switch (icon) {
  case ICON_BLOCK_FILLED:
    viacast_lcd_chr(drvthis, x, y, 0x1f);
    break;
  case ICON_ARROW_UP:
    viacast_lcd_chr(drvthis, x, y, 0x1e);
    break;
  case ICON_ARROW_DOWN:
    viacast_lcd_chr(drvthis, x, y, 0x1d);
    break;
  case ICON_ARROW_LEFT:
    viacast_lcd_chr(drvthis, x, y, 0x17);
    break;
  case ICON_ARROW_RIGHT:
    viacast_lcd_chr(drvthis, x, y, 0x18);
    break;
  case ICON_CHECKBOX_OFF:
    viacast_lcd_chr(drvthis, x, y, 0x1a);
    break;
  case ICON_CHECKBOX_ON:
    viacast_lcd_chr(drvthis, x, y, 0x19);
    break;
  case ICON_SELECTOR_AT_LEFT:
    viacast_lcd_chr(drvthis, x, y, 0x16);
    break;
  case ICON_SELECTOR_AT_RIGHT:
    viacast_lcd_chr(drvthis, x, y, 0x15);
    break;
  case ICON_CHECKBOX_GRAY:
    viacast_lcd_chr(drvthis, x, y, 0x1b);
    break;
  default:
    return -1; /* Let the core do other icons */
  }
  return 0;
}

/**
 * Draw a horizontal bar to the right.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is long at 100%
 * \param promille Current length level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void viacast_lcd_hbar(Driver *drvthis, int x, int y, int len,
                                    int promille, int options)
{
  lib_hbar_static(drvthis, x, y, len, promille, options, 5, 0x0f);
}

/**
 * Draw a vertical bar bottom-up.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is high at 100%
 * \param promille Current height level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void viacast_lcd_vbar(Driver *drvthis, int x, int y, int len,
                                    int promille, int options)
{
  lib_vbar_static(drvthis, x, y, len, promille, options, 5, 0x0a);
}

/**
 * Retrieve rotate
 * \param drvthis  Pointer to driver structure.
 * \return Stored rotate in promille.
 */
MODULE_EXPORT int viacast_lcd_get_rotate(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->rotate;
}

/**
 * Set rotate
 * \param drvthis  Pointer to driver structure.
 * \param rotate Set new rotate
 */
MODULE_EXPORT void viacast_lcd_set_rotate(Driver *drvthis, int rotate)
{
  PrivateData *p = drvthis->private_data;

  if ((rotate < 0) || (rotate > 3))
    return;

  if ((rotate == 1) || (rotate == 3)) {
    p->resize = 1;
  }
  else {
    p->resize = 0;
  }

  p->rotate = rotate;
  viacast_lcd_setup_gfxprim(drvthis);
}

/**
 * Retrieve display text
 * \param drvthis  Pointer to driver structure.
 * \return Stored rotate in promille.
 */
MODULE_EXPORT int viacast_lcd_get_display_text(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->always_text_bar;
}

/**
 * Set rotate
 * \param drvthis  Pointer to driver structure.
 * \param rotate Set new rotate
 */
MODULE_EXPORT void viacast_lcd_set_display_text(Driver *drvthis,
                                                int always_text)
{
  PrivateData *p = drvthis->private_data;

  if ((always_text < 0) || (always_text > 1))
    return;

  p->always_text_bar = always_text;
}

/**
 * Retrieve display text
 * \param drvthis  Pointer to driver structure.
 * \return Stored rotate in promille.
 */
MODULE_EXPORT int viacast_lcd_get_display_status_bar(Driver *drvthis)
{
  PrivateData *p = drvthis->private_data;

  return p->always_status_bar;
}

/**
 * Set rotate
 * \param drvthis  Pointer to driver structure.
 * \param rotate Set new rotate
 */
MODULE_EXPORT void viacast_lcd_set_display_status_bar(Driver *drvthis,
                                                int always_status_bar)
{
  PrivateData *p = drvthis->private_data;

  if ((always_status_bar < 0) || (always_status_bar > 1))
    return;

  p->always_status_bar = always_status_bar;
}



MODULE_EXPORT const char *viacast_lcd_get_pretty_name(Driver *drvthis)
{
  return "Viacast";
}
