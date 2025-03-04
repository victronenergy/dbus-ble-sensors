#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "safiery.h"
#include "task.h"

#define HW_ID_TOPDOWN_BLE		10

#define FLUID_TYPE_FRESH_WATER		1
#define FLUID_TYPE_WASTE_WATER		2
#define FLUID_TYPE_LIVE_WELL		3
#define FLUID_TYPE_OIL			4
#define FLUID_TYPE_BLACK_WATER		5
#define FLUID_TYPE_GASOLINE		6
#define FLUID_TYPE_DIESEL		7
#define FLUID_TYPE_LPG			8
#define FLUID_TYPE_LNG			9
#define FLUID_TYPE_HYDRAULIC_OIL	10
#define FLUID_TYPE_RAW_WATER		11

static struct VeSettingProperties capacity_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0.2,
	.min.value.Float	= 0,
	.max.value.Float	= 1000,
};

static struct VeSettingProperties fluid_type_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= INT32_MAX - 3,
};

static struct VeSettingProperties empty_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= 0,
	.max.value.Float	= 500,
};

static struct VeSettingProperties full_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 20,
	.min.value.Float	= 0,
	.max.value.Float	= 500,
};

static const struct dev_setting safiery_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
};

static const struct dev_setting safiery_topdown_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &full_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &empty_props,
	},
};

static int safiery_init(struct VeItem *root, void *data)
{
	VeVariant v;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);
	ble_dbus_set_item(root, "Level",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitNone);

	ble_dbus_add_settings(root, safiery_topdown_settings,
			      array_size(safiery_topdown_settings));

	return 0;
}

static const struct reg_info safiery_adv[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
		.mask	= 0x7f,
		.name	= "HardwareID",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 1,
		.mask	= 0x7f,
		.scale	= 32,
		.name	= "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.mask	= 0x7f,
		.scale	= 1,
		.bias	= -40,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.shift	= 7,
		.mask	= 1,
		.name	= "SyncButton",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.mask	= 0x3fff,
		.scale	= 10,
		.name	= "RawValue",
		.format	= &veUnitcm,
	},
	{
		.type	= VE_SN8,
		.offset	= 8,
		.scale	= 1024,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 9,
		.scale	= 1024,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 10,
		.shift	= 1024,
		.name	= "AccelZ",
		.format	= &veUnitG2Dec,
	},
};

static const struct dev_info safiery_sensor = {
	.product_id	= VE_PROD_ID_SAFIERY_TANK_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "safiery_",
	.role		= "tank",
	.num_settings	= array_size(safiery_settings),
	.settings	= safiery_settings,
	.num_regs	= array_size(safiery_adv),
	.regs		= safiery_adv,
	.init		= safiery_init,
};

static void safiery_update_level(struct VeItem *root)
{
	struct VeItem *item;
	float capacity;
	int height;
	int empty;
	int full;
	float level;
	float remain;
	VeVariant v;

	item = veItemByUid(root, "Capacity");
	if (!item)
		return;

	veItemLocalValue(item, &v);
	veVariantToFloat(&v);
	capacity = v.value.Float;

	height = veItemValueInt(root, "RawValue");
	empty = veItemValueInt(root, "RawValueEmpty");
	full = veItemValueInt(root, "RawValueFull");

	if (empty <= full)
		goto out_inval;

	level = (float)(height - empty) / (full - empty);

	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;

	remain = level * capacity;

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));

	return;

out_inval:
	veItemInvalidate(veItemByUid(root, "Level"));
	veItemInvalidate(veItemByUid(root, "Remaining"));
	ble_dbus_set_int(root, "Status", 4);
}

int safiery_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid = buf + 5;
	char name[24];
	char dev[16];
	int hwid;

	if (len != 10)
		return -1;

	if (uid[0] != addr->b[2] ||
	    uid[1] != addr->b[1] ||
	    uid[2] != addr->b[0])
		return -1;

	hwid = buf[0];

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &safiery_sensor, (void *)hwid);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "StarTank %02X:%02X:%02X",
		uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, buf, len);

	safiery_update_level(root);
	ble_dbus_update(root);

	return 0;
}

