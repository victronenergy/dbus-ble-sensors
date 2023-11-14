#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "mopeka.h"
#include "task.h"

#define HW_ID_LPG	3
#define HW_ID_AIR	4
#define HW_ID_H2O	5

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
	.max.value.Float	= 150,
};

static struct VeSettingProperties full_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 20,
	.min.value.Float	= 0,
	.max.value.Float	= 150,
};

static const struct dev_setting mopeka_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
	{
		.name	= "RawValueEmpty",
		.props	= &empty_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &full_props,
	},
};

static struct VeSettingProperties butane_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 0,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static const struct dev_setting mopeka_lpg_settings[] = {
	{
		.name	= "ButaneRatio",
		.props	= &butane_props,
	},
};

static int mopeka_init(struct VeItem *root, void *data)
{
	int hwid = (int)data;
	VeVariant v;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);
	ble_dbus_set_item(root, "Level",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitNone);

	if (hwid == HW_ID_LPG) {
		ble_dbus_add_settings(root, mopeka_lpg_settings,
				      array_size(mopeka_lpg_settings));
	}

	return 0;
}

static const struct dev_info mopeka_sensor = {
	.product_id	= VE_PROD_ID_MOPEKA_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "mopeka_",
	.role		= "tank",
	.num_settings	= array_size(mopeka_settings),
	.settings	= mopeka_settings,
	.init		= mopeka_init,
};

static const float mopeka_coefs_h2o[] = {
	0.600592, 0.003124, -0.00001368,
};

static const float mopeka_coefs_lpg[] = {
	0.573045, -0.002822, -0.00000535,
};

static const float mopeka_coefs_butane[] = {
	0.03615, 0.000815,
};

static float mopeka_scale_butane(struct VeItem *root, int temp)
{
	float r = veItemValueInt(root, "ButaneRatio") / 100.0;

	return mopeka_coefs_butane[0] * r + mopeka_coefs_butane[1] * r * temp;
}

static int mopeka_xlate_level(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	const float *coefs;
	float scale = 0;
	float level;
	int hwid;
	int temp;

	hwid = veItemValueInt(root, "HardwareID");
	temp = veItemValueInt(root, "Temperature");
	temp += 40;

	switch (hwid) {
	case HW_ID_LPG:
		scale = mopeka_scale_butane(root, temp);
		coefs = mopeka_coefs_lpg;
		break;
	case HW_ID_H2O:
		coefs = mopeka_coefs_h2o;
		break;
	default:
		return -1;
	}

	scale += coefs[0] + coefs[1] * temp + coefs[2] * temp * temp;
	level = rv * scale;
	veVariantFloat(val, level / 10);

	return 0;
}

static const struct reg_info mopeka_adv[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
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
		.xlate	= mopeka_xlate_level,
		.name	= "RawValue",
		.format	= &veUnitcm,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.shift	= 14,
		.mask	= 3,
		.name	= "Quality",
		.format	= &veUnitNone,
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
};

static void mopeka_update_level(struct VeItem *root)
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

	if (empty >= full) {
		veItemInvalidate(veItemByUid(root, "Level"));
		veItemInvalidate(veItemByUid(root, "Remaining"));
		ble_dbus_set_int(root, "Status", 4);
		return;
	}

	level = (float)(height - empty) / (full - empty);

	if (level < 0)
		level = 0;
	if (level > 1)
		level = 1;

	remain = level * capacity;

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));
}

int mopeka_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid = buf + 5;
	const char *type;
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

	switch (hwid) {
	case HW_ID_LPG:
		type = "LPG";
		break;
	case HW_ID_H2O:
		type = "H2O";
		break;
	default:
		return -1;
	}

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &mopeka_sensor, (void *)hwid);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "Mopeka %s %02X:%02X:%02X",
		 type, uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, mopeka_adv, array_size(mopeka_adv), buf, len);

	mopeka_update_level(root);
	ble_dbus_update(root);

	return 0;
}

