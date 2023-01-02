#include "viacast_lcd_utils.h"

int filter(const struct dirent *name)
{
  if (name->d_type != DT_REG)
    return 0;
}

void /* Display information from inotify_event structure */
displayInotifyEvent(struct inotify_event *i, int *scandir)
{
  if (i->mask & IN_CREATE)
    *scandir = 1;

  if (i->mask & IN_DELETE)
    *scandir = 1;

  if (i->mask & IN_MODIFY)
    *scandir = 1;

}