#include <stdio.h>
#include <stdint.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "garnet.h"
#include "tank.h"

#define GARNET_709_SHORT_CIRCUIT   101
#define GARNET_709_OPEN            102
#define GARNET_709_BIT_COUNT_ERROR 103
#define GARNET_709_STACK_MISCONFIG 104
#define GARNET_709_MISSING_TOP     105
#define GARNET_709_MISSING_BOTTOM  106
#define GARNET_709_NO_CALIBRATION  107
#define GARNET_709_BAD_CHECKSUM    108
#define GARNET_709_OUT_OF_RANGE    109
#define GARNET_709_DISABLED        110
#define GARNET_709_INIT            111
#define GARNET_709_BAD_ID          112

/*
 * Manufacturer Specific Data (after the 2-byte Company ID) is 12 bytes:
 *   0-2  : Serial Number
 *   3    : Fresh1 %
 *   4    : Grey1 %
 *   5    : Black1 %
 *   6    : Fresh2 %
 *   7    : Grey2 %
 *   8    : Black2 %
 *   9    : Galley %
 *   10   : LPG %
 *   11   : Battery V * 10 (14.1V => 141)
 */

static int garnet_level(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (rawval > 100)
		return -1;

	veVariantFloat(val, rawval);

	return 0;
}

#define GARNET_LEVEL(i) {			\
		.type	= VE_UN8,		\
		.offset	= 3 + i,		\
		.shift	= 0,			\
		.bits	= 8,			\
		.flags	= REG_FLAG_KEY,		\
		.key	= i,			\
		.name	= "RawValue",		\
		.format	= &veUnitPercentage,	\
		.xlate	= garnet_level,		\
	}

static const struct reg_info garnet_adv[] = {
	{
		.type	= VE_UN32,
		.offset	= 0,
		.bits	= 24,
		.name	= "HardwareID",
		.format	= &veUnitNone,
	},
	GARNET_LEVEL(0),
	GARNET_LEVEL(1),
	GARNET_LEVEL(2),
	GARNET_LEVEL(3),
	GARNET_LEVEL(4),
	GARNET_LEVEL(5),
	GARNET_LEVEL(6),
	GARNET_LEVEL(7),
	{
		.type	= VE_UN8,
		.offset	= 11,
		.bits	= 8,
		.scale	= 10,
		.name	= "BatteryVoltage",
		.format	= &veUnitVolt1Dec,
	},
};

static const struct tank_info garnet_tank_info = {
	.raw_unit	= "%",
	.raw_min	= 0,
	.raw_max	= 100,
	.raw_empty	= 0,
	.raw_full	= 100,
};

#define GARNET_SENSOR(i) {					\
		.product_id	= VE_PROD_ID_GARNET_SEELEVEL,	\
		.dev_class	= &tank_class,			\
		.dev_instance	= 20,				\
		.dev_prefix	= "garnet_",			\
		.reg_key	= i,				\
		.num_regs	= array_size(garnet_adv),	\
		.regs		= garnet_adv,			\
	}

static const struct dev_info garnet_sensor[] = {
	GARNET_SENSOR(0),
	GARNET_SENSOR(1),
	GARNET_SENSOR(2),
	GARNET_SENSOR(3),
	GARNET_SENSOR(4),
	GARNET_SENSOR(5),
	GARNET_SENSOR(6),
	GARNET_SENSOR(7),
};

static const char *garnet_names[] = {
	"Fresh 1",
	"Grey 1",
	"Black 1",
	"Fresh 2",
	"Grey 2",
	"Black 2",
	"Galley",
	"LPG",
};

int garnet_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *droot;
	char name[32];
	char dev[20];
	int serial;
	int i;

	if (len < 12)
		return -1;

	serial = buf[0] | (buf[1] << 8) | (buf[2] << 16);

	for (i = 0; i < 8; i++) {
		if (buf[3 + i] == GARNET_709_DISABLED)
			continue;

		snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x_%d",
			 addr->b[5], addr->b[4], addr->b[3],
			 addr->b[2], addr->b[1], addr->b[0], i);
		snprintf(name, sizeof(name), "SeeLeveL Soul %d %s", serial,
			 garnet_names[i]);

		droot = ble_dbus_create(dev, &garnet_sensor[i],
					&garnet_tank_info);
		if (!droot)
			return -1;

		ble_dbus_set_name(droot, name);

		if (!ble_dbus_is_enabled(droot))
			continue;

		ble_dbus_set_regs(droot, buf, len);
		ble_dbus_update(droot);
	}

	return 0;
}
