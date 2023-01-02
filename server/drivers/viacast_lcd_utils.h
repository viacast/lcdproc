#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>

int filter(const struct dirent *name);

void displayInotifyEvent(struct inotify_event *i, int *rescan);