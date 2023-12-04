#include "viacast_lcd_utils.h"
#include <stdint.h>

void appendValueBattery(Battery* battery, uint8_t value) {
    if (value > MAX_BATTERY){
      value = MAX_BATTERY;
    }
    
    if (value < MIN_BATTERY){
      value = MIN_BATTERY;
    }
    
    battery->battery_values[battery->head] = value;
    battery->head = (battery->head + 1) % SIZE;
}

uint8_t getMeanBattery(Battery* battery) {
    int count = 0;
    uint16_t sum = 0;

    for (int i = 0; i < SIZE; ++i) {
        
        if ( battery->battery_values[i] <= 0 ){
          continue;
        }

        sum += battery->battery_values[i];
        count++;
    }

    return count > 0 ? (uint8_t)sum / count : 0;
}

int tryUpdateBatteryValue(Battery* battery){
  uint8_t mean_battery = getMeanBattery(battery);
  uint8_t delta = battery->battery_current - mean_battery;
  if ( delta > MAX_DELTA || delta < MAX_DELTA) {
    battery->battery_current = mean_battery;
    return 1;
  }
  return 0;
}

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