#include "viacast_lcd_utils.h"

int filter(const struct dirent *name)
{
  if (name->d_type != DT_REG)
    return 0;

  return 1;
}

void /* Display information from inotify_event structure */
check_inotify_event(struct inotify_event *i, int *reload_icons)
{

  if (i->mask & IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO |
      IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)

    *reload_icons = 1;
}