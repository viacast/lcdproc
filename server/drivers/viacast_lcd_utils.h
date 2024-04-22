#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/types.h>

#define SIZE 3
#define MAX_DELTA 1
#define N_BATTERY_STATE 4

/**
 * Represent a battery
 * \param state     State of battery
 *   state:
 *    0: font
 *    1: > up 75
 *    2: > between 50 and 75
 *    3: > between 25 and 50
 *    4: > down 25
 *    5: = 0
 */
typedef struct {
  int state; /* 0: font;
    1:  up 75;
    2:  between 50 and 75;
    3:  between 25 and 50;
    4:  down 25;
    5:  equal 0   
    */

  uint16_t is_drain_ext_battery;
  uint16_t voltage_ext_battery;
  uint16_t voltage_int_battery;
  uint16_t is_power_supply;
  uint16_t voltage_power_supply;

  u_int8_t cycles_to_read;
  
  int new_state;
  uint16_t max_battery;
  uint16_t min_battery;
  uint16_t min_font;
  uint16_t battery_values[SIZE];
  uint16_t battery_current;
  uint32_t battery_percentual;
  uint16_t head; // Points to the position to insert the next element
} Battery;

int filter(const struct dirent *name);

bool writeInFile(const char * filename, char *content);

void check_inotify_event(struct inotify_event *i, int *reload_icons);

void updateBattery(Battery* battery);