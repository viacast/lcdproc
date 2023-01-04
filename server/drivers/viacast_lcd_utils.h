#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>

int filter(const struct dirent *name);

void check_inotify_event(struct inotify_event *i, int *reload_icons);