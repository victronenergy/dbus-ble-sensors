#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ble-dbus.h"
#include "tank.h"
#include "task.h"

/* Forward declarations for internal functions */
static inline const struct dev_info *get_dev_info(struct VeItem *root)
{
	return veItemCtx(root)->ptr;
}

static void tank_setting_changed(struct VeItem *root, struct VeItem *setting,
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

float tank_shape_calculate_volume(const struct tank_shape *shape, float height)
{
	int i;

	if (shape->num_points == 0) {
		return height;
	}

	if (height <= 0)
		return 0;

	if (height >= 100)
		return 100;

	if (height <= shape->points[0].height) {
		float h1 = 0;
		float h2 = shape->points[0].height;
		float v1 = 0;
		float v2 = shape->points[0].volume;

		if (h2 == h1)
			return v1;

		return v1 + (v2 - v1) * (height - h1) / (h2 - h1);
	}

	if (height >= shape->points[shape->num_points - 1].height) {
		float h1 = shape->points[shape->num_points - 1].height;
		float h2 = 100;
		float v1 = shape->points[shape->num_points - 1].volume;
		float v2 = 100;

		if (h2 == h1)
			return v1;

		return v1 + (v2 - v1) * (height - h1) / (h2 - h1);
	}

	for (i = 0; i < shape->num_points - 1; i++) {
		if (height >= shape->points[i].height &&
			height <= shape->points[i + 1].height) {
			float h1 = shape->points[i].height;
			float h2 = shape->points[i + 1].height;
			float v1 = shape->points[i].volume;
			float v2 = shape->points[i + 1].volume;

			if (h2 == h1)
				return v1;

			return v1 + (v2 - v1) * (height - h1) / (h2 - h1);
		}
	}

	return height;
}

float tank_shape_calculate_remaining(struct VeItem *root, float raw_height)
{
	struct tank_shape shape;
	struct VeItem *shape_item;
	float capacity;
	float empty, full;
	float height_pct;
	float volume_pct;
	VeVariant val;

	shape_item = veItemByUid(root, "Shape");
	if (shape_item && veItemIsValid(shape_item)) {
		veItemLocalValue(shape_item, &val);
		if (veVariantIsValid(&val) && val.value.Ptr && strlen(val.value.Ptr) > 0) {
			if (tank_shape_parse_from_string(val.value.Ptr, &shape) < 0) {
				goto linear_fallback;
			}

			capacity = veItemValueFloat(root, "Capacity");
			empty = veItemValueFloat(root, "RawValueEmpty");
			full = veItemValueFloat(root, "RawValueFull");

			if (capacity <= 0 || empty == full) {
				return 0;
			}

			height_pct = 100.0 * (raw_height - empty) / (full - empty);

			if (height_pct < 0)
				height_pct = 0;
			if (height_pct > 100)
				height_pct = 100;

			volume_pct = tank_shape_calculate_volume(&shape, height_pct);

			if (volume_pct < 0)
				volume_pct = 0;
			if (volume_pct > 100)
				volume_pct = 100;

			return capacity * volume_pct / 100.0;
		}
	}

linear_fallback:
	capacity = veItemValueFloat(root, "Capacity");
	empty = veItemValueFloat(root, "RawValueEmpty");
	full = veItemValueFloat(root, "RawValueFull");

	if (capacity <= 0 || empty == full)
		return 0;

	height_pct = (raw_height - empty) / (full - empty);

	if (height_pct < 0)
		height_pct = 0;
	if (height_pct > 1)
		height_pct = 1;

	return height_pct * capacity;
}

int tank_shape_parse_from_string(const char *shape_str, struct tank_shape *shape)
{
	char *str_copy, *saveptr, *token;
	int i = 0;
	int str_len;

	shape->type = TANK_SHAPE_CUSTOM;
	shape->num_points = 0;

	if (!shape_str)
		return -1;

	str_len = strnlen(shape_str, 1000);
	if (str_len == 0 || str_len >= 1000)
		return -1;

	for (int j = 0; j < str_len; j++) {
		char c = shape_str[j];
		if (!(isdigit(c) || c == ':' || c == ',' || c == ' ')) {
			return -1;
		}
	}

	str_copy = malloc(str_len + 1);
	if (!str_copy)
		return -1;

	memcpy(str_copy, shape_str, str_len);
	str_copy[str_len] = '\0';

	token = strtok_r(str_copy, ",", &saveptr);
	while (token && i < MAX_SHAPE_POINTS) {
		char *colon = strchr(token, ':');
		if (!colon) {
			free(str_copy);
			return -1;
		}

		*colon = '\0';
		int sensor_level = atoi(token);
		int volume = atoi(colon + 1);

		if (sensor_level < 1 || sensor_level > 99 ||
			volume < 1 || volume > 99) {
			free(str_copy);
			return -1;
		}

		shape->points[i].height = (float)sensor_level;
		shape->points[i].volume = (float)volume;
		shape->num_points++;
		i++;

		token = strtok_r(NULL, ",", &saveptr);
	}

	free(str_copy);
	return shape->num_points > 0 ? 0 : -1;
}

void tank_shape_init_settings(struct VeItem *root)
{
	VeVariant v;

	ble_dbus_set_item(root, "Shape",
			  veVariantHeapStr(&v, ""),
			  &veUnitNone);
}

static void tank_init(struct VeItem *root, const void *data)
{
	const struct tank_info *ti = data;
	const char *dev = veItemId(root);
	const struct dev_info *info = get_dev_info(root);
	struct VeItem *settings = get_settings();
	char path[128];
	VeVariant v;

	ble_dbus_set_str(root, "RawUnit", "cm");
	ble_dbus_set_item(root, "Remaining",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitm3);
	ble_dbus_set_item(root, "Level",
			  veVariantInvalidType(&v, VE_FLOAT), &veUnitNone);

	snprintf(path, sizeof(path), "Settings/Devices/%s%s",
		 info->dev_prefix, dev);
	veItemCreateSettingsProxy(settings, path, root, "Shape",
				  veVariantFmt, &veUnitNone, &shape_props);

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
	struct VeItem *shape_item;
	float capacity;
	float height;
	float empty;
	float full;
	float level;
	float remain;
	VeVariant v, shape_val;
	int has_custom_shape = 0;

	height = veItemValueFloat(root, "RawValue");

	shape_item = veItemByUid(root, "Shape");
	if (shape_item && veItemIsValid(shape_item)) {
		veItemLocalValue(shape_item, &shape_val);
		if (veVariantIsValid(&shape_val) && shape_val.value.Ptr && strlen(shape_val.value.Ptr) > 0) {
			has_custom_shape = 1;
		}
	}

	if (has_custom_shape) {
		remain = tank_shape_calculate_remaining(root, height);
		capacity = veItemValueFloat(root, "Capacity");

		if (capacity > 0)
			level = remain / capacity;
		else
			level = 0;
	} else {
		capacity = veItemValueFloat(root, "Capacity");
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
	}

	ble_dbus_set_int(root, "Level", lrintf(100 * level));

	item = veItemByUid(root, "Remaining");
	veItemOwnerSet(item, veVariantFloat(&v, remain));

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

const struct dev_class tank_class = {
	.role		= "tank",
	.num_settings	= array_size(tank_settings),
	.settings	= tank_settings,
	.init		= tank_init,
	.update		= tank_update,
};
