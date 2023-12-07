#include "viacast_lcd_utils.h"
#include <stdint.h>

void appendValueBattery(Battery *battery, uint8_t value) {
  if (value > battery->max_battery) {
    value = battery->max_battery;
  }

  if (value < battery->min_battery) {
    value = battery->min_battery;
  }

  battery->battery_values[battery->head] = value;
  battery->head = (battery->head + 1) % SIZE;
}

uint8_t getMeanBattery(Battery *battery) {
  int count = 0;
  int sum = 0;
  uint8_t result = 0;
  for (int i = 0; i < SIZE; ++i) {
    if (battery->battery_values[i] <= 0) {
      continue;
    }

    sum += battery->battery_values[i];
    count++;
  }
  result = count > 0 ? (uint8_t)(sum / count) : 0;
  return result;
}

int tryUpdateBatteryValue(Battery *battery) {
  uint8_t mean_battery = getMeanBattery(battery);
  int32_t delta = battery->battery_current - mean_battery;

  if (delta > MAX_DELTA || delta < -MAX_DELTA) {
    battery->battery_current = mean_battery;
    return 1;
  }
  return 0;
}

int filter(const struct dirent *name) {
  if (name->d_type != DT_REG)
    return 0;

  return 1;
}

void /* Display information from inotify_event structure */
check_inotify_event(struct inotify_event *i, int *reload_icons) {

  if (i->mask & IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO |
      IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)

    *reload_icons = 1;
}

void updateBattery(Battery *battery, uint8_t battery_read) {

  uint8_t interval =
      ((battery->max_battery - battery->min_battery) / N_BATTERY_STATE);

  fprintf(stderr, "Before interval: %u, current: %u, min_battery: %u\n", interval,
          battery->battery_current, battery->min_battery);

  if (battery_read > battery->min_font) {
    battery->new_state = 0;
    if (battery->state != 0) {
      memset(battery->battery_values, 0, sizeof(battery->battery_values));
    }
    return;
  }

  appendValueBattery(battery, battery_read);
  if (!tryUpdateBatteryValue(battery)) {
    battery->new_state = battery->state;
    return;
  }


  
  fprintf(stderr, "After interval: %u, current: %u, min_battery: %u\n", interval,
          battery->battery_current, battery->min_battery);
  
  battery->new_state =
      battery->battery_current <= battery->min_battery + (0 * interval)   ? 5
      : battery->battery_current <= battery->min_battery + (1 * interval) ? 4
      : battery->battery_current <= battery->min_battery + (2 * interval) ? 3
      : battery->battery_current <= battery->min_battery + (3 * interval) ? 2
                                                                          : 1;
}