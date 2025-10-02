#ifndef ANGLE_H
#define ANGLE_H

#include "ble-dbus.h"

struct angle_info {
	uint32_t	flags;
};

int angle_add_settings(struct VeItem *root);
void angle_init(struct VeItem *root);
void angle_calculate(struct VeItem *root);

#endif
