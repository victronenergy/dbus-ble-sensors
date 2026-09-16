#ifndef VICTRON_BATTERY_MONITOR_H
#define VICTRON_BATTERY_MONITOR_H

#include "victron.h"

extern const struct victron_device battery_monitor_victron_device;
extern veBool battery_monitor_is_supported(const uint8_t *buf, int len);

#endif
