#include "viacast_lcd_utils.h"

int filter(const struct dirent *name)
{
  if (name->d_type != DT_REG)
    return 0;
}

void /* Display information from inotify_event structure */
check_inotify_event(struct inotify_event *i, int *reload_icons)
{
  if (i->mask & IN_ALL_EVENTS)
    *reload_icons = 1;
}