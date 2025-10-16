#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ble-dbus.h"
#include "tank.h"

#define TANK_SHAPE_MAX_POINTS		10

struct tank_data {
	int		shape_map_len;
	float		shape_map[TANK_SHAPE_MAX_POINTS + 2][2];
};

static void tank_setting_changed(struct VeItem *root, struct VeItem *setting,
				 const void *data);
static void tank_shape_changed(struct VeItem *root, struct VeItem *setting,
			       const void *data);

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

static struct VeSettingProperties shape_props = {
	.type			= VE_HEAP_STR,
	.def.value.Ptr		= "",
};

static const struct dev_setting tank_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_props,
		.onchange = tank_setting_changed,
	},
	{
		.name	= "FluidType",
		.props	= &fluid_type_props,
	},
	{
		.name	= "Shape",
		.props	= &shape_props,
		.onchange = tank_shape_changed,
	},
};

static const struct dev_setting tank_bottomup_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &empty_props,
		.onchange = tank_setting_changed,
	},
	{
		.name	= "RawValueFull",
		.props	= &full_props,
		.onchange = tank_setting_changed,
	},
};

static const struct dev_setting tank_topdown_settings[] = {
	{
		.name	= "RawValueEmpty",
		.props	= &full_props,
		.onchange = tank_setting_changed,
	},
	{
		.name	= "RawValueFull",
		.props	= &empty_props,
		.onchange = tank_setting_changed,
	},
};

static struct VeSettingProperties high_active_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 90,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static struct VeSettingProperties high_restore_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 80,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static struct VeSettingProperties low_active_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 10,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static struct VeSettingProperties low_restore_props = {
	.type			= VE_SN32,
	.def.value.SN32		= 15,
	.min.value.SN32		= 0,
	.max.value.SN32		= 100,
};

static const struct alarm tank_alarms[] = {
	{
		.name	= "High",
		.item	= "Level",
		.flags	= ALARM_FLAG_HIGH | ALARM_FLAG_CONFIG,
		.active	= &high_active_props,
		.restore = &high_restore_props,
	},
	{
		.name	= "Low",
		.item	= "Level",
		.flags	= ALARM_FLAG_CONFIG,
		.active	= &low_active_props,
		.restore = &low_restore_props,
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
	struct tank_data *td = ble_dbus_get_cdata(root);
	struct VeItem *item;
	float capacity;
	float height;
	float empty;
	float full;
	float level;
	float remain;
	VeVariant v;
	int i;

	item = veItemByUid(root, "RawValue");
	if (!item || !veItemIsValid(item))
		goto out_inval;

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

	for (i = 1; i < td->shape_map_len; i++) {
		if (td->shape_map[i][0] >= level) {
			float s0 = td->shape_map[i - 1][0];
			float s1 = td->shape_map[i    ][0];
			float l0 = td->shape_map[i - 1][1];
			float l1 = td->shape_map[i    ][1];
			level = l0 + (level - s0) / (s1 - s0) * (l1 - l0);
			break;
		}
	}

	remain = level * capacity;

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));

	ble_dbus_set_int(root, "Status", STATUS_OK);

	return;

out_inval:
	veItemInvalidate(veItemByUid(root, "Level"));
	veItemInvalidate(veItemByUid(root, "Remaining"));
	ble_dbus_set_int(root, "Status", 4);
}

static void tank_setting_changed(struct VeItem *root, struct VeItem *setting,
				 const void *data)
{
	tank_update(root, data);
	veItemSendPendingChanges(root);
}

static void tank_shape_changed(struct VeItem *root, struct VeItem *setting,
			       const void *data)
{
	struct tank_data *td = ble_dbus_get_cdata(root);
	VeVariant shape;
	const char *map;
	int i;

	if (!veVariantIsValid(veItemLocalValue(setting, &shape))) {
		fprintf(stderr, "invalid shape value\n");
		goto reset;
	}

	map = shape.value.Ptr;

	if (!map[0])
		goto reset;

	td->shape_map[0][0] = 0;
	td->shape_map[0][1] = 0;
	i = 1;

	while (i < TANK_SHAPE_MAX_POINTS) {
		unsigned int s, l;

		if (sscanf(map, "%u:%u", &s, &l) < 2) {
			fprintf(stderr, "malformed shape spec\n");
			goto reset;
		}

		if (s < 1 || s > 99 || l < 1 || l > 99) {
			fprintf(stderr, "shape level out of range 1-99\n");
			goto reset;
		}

		if (s <= td->shape_map[i - 1][0] ||
		    l <= td->shape_map[i - 1][1]) {
			fprintf(stderr, "shape level non-increasing\n");
			goto reset;
		}

		td->shape_map[i][0] = s / 100.0;
		td->shape_map[i][1] = l / 100.0;
		i++;

		map = strchr(map, ',');
		if (!map)
			break;

		map++;
	}

	td->shape_map[i][0] = 1;
	td->shape_map[i][1] = 1;
	td->shape_map_len = i + 1;

out:
	tank_setting_changed(root, setting, data);
	return;

reset:
	td->shape_map_len = 0;
	goto out;
}

const struct dev_class tank_class = {
	.role		= "tank",
	.num_settings	= array_size(tank_settings),
	.settings	= tank_settings,
	.num_alarms	= array_size(tank_alarms),
	.alarms		= tank_alarms,
	.init		= tank_init,
	.update		= tank_update,
	.pdata_size	= sizeof(struct tank_data),
};
