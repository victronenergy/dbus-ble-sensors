#include <math.h>

#include "ble-dbus.h"
#include "tank.h"

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

static const struct dev_setting tank_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
};

static const struct dev_setting tank_bottomup_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &empty_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &full_props,
	},
};

static const struct dev_setting tank_topdown_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &full_props,
	},
	{
		.name	= "RawValueFull",
		.props	= &empty_props,
	},
};

static void tank_init(struct VeItem *root, const void *data)
{
	const struct tank_info *ti = data;
	VeVariant v;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);
	ble_dbus_set_item(root, "Level",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitNone);

	if (ti->flags & TANK_FLAG_TOPDOWN)
		ble_dbus_add_settings(root, tank_topdown_settings,
				      array_size(tank_topdown_settings));
	else
		ble_dbus_add_settings(root, tank_bottomup_settings,
				      array_size(tank_bottomup_settings));
}

static void tank_update(struct VeItem *root, const void *data)
{
	const struct tank_info *ti = data;
	struct VeItem *item;
	float capacity;
	float height;
	float empty;
	float full;
	float level;
	float remain;
	VeVariant v;

	capacity = veItemValueFloat(root, "Capacity");
	height = veItemValueFloat(root, "RawValue");
	empty = veItemValueFloat(root, "RawValueEmpty");
	full = veItemValueFloat(root, "RawValueFull");

	if (ti->flags & TANK_FLAG_TOPDOWN) {
		if (empty <= full)
			goto out_inval;
	} else {
		if (empty >= full)
			goto out_inval;
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

	return;

out_inval:
	veItemInvalidate(veItemByUid(root, "Level"));
	veItemInvalidate(veItemByUid(root, "Remaining"));
	ble_dbus_set_int(root, "Status", 4);
}

const struct dev_class tank_class = {
	.role		= "tank",
	.num_settings	= array_size(tank_settings),
	.settings	= tank_settings,
	.init		= tank_init,
	.update		= tank_update,
};
