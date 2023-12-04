#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>

#define SIZE 10
#define MAX_DELTA 2
#define MAX_BATTERY 148
#define MIN_BATTERY 120
#define N_BATTERY_STATE 4

typedef struct {
    uint8_t battery_values[SIZE];
    uint8_t battery_current;
    uint8_t head;  // Points to the position to insert the next element
} Battery;

int filter(const struct dirent *name);

void check_inotify_event(struct inotify_event *i, int *reload_icons);   