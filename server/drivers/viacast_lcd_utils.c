#include "viacast_lcd_utils.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

bool writeInFile(const char *filename, char *content) {
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    return false;
  }

  fprintf(fp, "%s", content);
  fclose(fp);
  return true;
}

bool readBatteryFromFile(const char *filename, ManagerBattery *man_battery) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    return false;
  }

  fscanf(fp, "%" SCNu16 ",%" SCNu16 ",%" SCNu16 ",%" SCNu16 ",%" SCNu16,
         &man_battery->is_drain_ext_battery, &man_battery->voltage_ext_battery,
         &man_battery->voltage_int_battery, &man_battery->is_power_supply,
         &man_battery->voltage_power_supply);
  fclose(fp);

  return true;
}

void appendValueBattery(ManagerBattery *man_battery, uint16_t valueext,
                        uint16_t valueint) {

  if (valueext > man_battery->max_battery) {
    valueext = man_battery->max_battery;
  }

  if (valueext < man_battery->min_battery) {
    valueext = man_battery->min_battery;
  }

  man_battery->external.battery_values[man_battery->external.head] = valueext;
  man_battery->external.head = (man_battery->external.head + 1) % SIZE;

  if (valueint > man_battery->max_battery) {
    valueint = man_battery->max_battery;
  }

  if (valueint < man_battery->min_battery) {
    valueint = man_battery->min_battery;
  }

  man_battery->internal.battery_values[man_battery->internal.head] = valueint;
  man_battery->internal.head = (man_battery->internal.head + 1) % SIZE;
}

uint16_t getMeanBattery(Battery *battery) {
  int count = 0;
  int sum = 0;
  uint16_t result = 0;
  for (int i = 0; i < SIZE; ++i) {
    if (battery->battery_values[i] <= 0) {
      continue;
    }

    sum += battery->battery_values[i];
    count++;
  }
  result = count > 0 ? (uint16_t)(sum / count) : 0;
  return result;
}

void tryUpdateBatteryCurrent(Battery *battery) {
  uint16_t mean_battery = getMeanBattery(battery);
  int32_t delta = battery->battery_current - mean_battery;

  if (delta > MAX_DELTA || delta < -MAX_DELTA) {
    battery->battery_current = mean_battery;
  }
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

void getPercentBattery(ManagerBattery *man_battery, Battery *battery) {

  battery->battery_percentual =
      (uint32_t)(((battery->battery_current - man_battery->min_battery) *
                  100U) /
                 (man_battery->max_battery - man_battery->min_battery));

  if (battery->battery_percentual > 100) {
    battery->battery_percentual = 100;
  }

  if (battery->battery_percentual < 0) {
    battery->battery_percentual = 0;
  }
}

bool updateBattery(ManagerBattery *man_battery) {

  if (man_battery->cycles_to_read < 10) {
    man_battery->cycles_to_read++;
    return false;
  }

  man_battery->cycles_to_read = 0;

  uint8_t interval =
      ((man_battery->max_battery - man_battery->min_battery) / N_BATTERY_STATE);

  if (!readBatteryFromFile("/tmp/battery-manager", man_battery)) {
    man_battery->n_tries_read_file_manager++;
    if (man_battery->n_tries_read_file_manager > 10){
      man_battery->is_file_manager = false;
    }
    return false;
  }

  man_battery->n_tries_read_file_manager = 0;
  man_battery->is_file_manager = true;

  appendValueBattery(man_battery, man_battery->voltage_ext_battery,
                     man_battery->voltage_int_battery);

  getPercentBattery(man_battery, &man_battery->external);
  getPercentBattery(man_battery, &man_battery->internal);

  tryUpdateBatteryCurrent(&man_battery->external);
  tryUpdateBatteryCurrent(&man_battery->internal);

  if (man_battery->is_power_supply == 1) {
    man_battery->new_state = 0;
    return true;
  }

  uint16_t current = man_battery->is_drain_ext_battery == 1
                         ? man_battery->external.battery_current
                         : man_battery->internal.battery_current;

  man_battery->new_state =
      current <= man_battery->min_battery + (0 * interval)   ? 5
      : current <= man_battery->min_battery + (1 * interval) ? 4
      : current <= man_battery->min_battery + (2 * interval) ? 3
      : current <= man_battery->min_battery + (3 * interval) ? 2
                                                             : 1;
  return true;
}