#include <stdio.h>
#include <math.h>

#include "ble-dbus.h"
#include "angle.h"

static void angle_setting_changed(struct VeItem *root, struct VeItem *setting,
				  const void *data);

static struct VeSettingProperties calculate_angle_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 0,
	.min.value.SN32	= 0,
	.max.value.SN32	= 1,
};

static struct VeSettingProperties calib_offset_props = {
	.type		= VE_FLOAT,
	.def.value.Float = 0.0,
	.min.value.Float = -10.0,
	.max.value.Float = 10.0,
};

static const struct dev_setting angle_settings[] = {
	{
		.name	= "CalculateAngles",
		.props	= &calculate_angle_props,
		.onchange = angle_setting_changed,
	},
	{
		.name	= "CalibX",
		.props	= &calib_offset_props,
	},
	{
		.name	= "CalibY",
		.props	= &calib_offset_props,
	},
	{
		.name	= "CalibZ",
		.props	= &calib_offset_props,
	},
};

int angle_add_settings(struct VeItem *root)
{
	return ble_dbus_add_settings(root, angle_settings,
				     array_size(angle_settings));
}

void angle_init(struct VeItem *root)
{
	VeVariant val;

	/* Create CalibrateAngles as a regular item (not a setting) */
	ble_dbus_set_item(root, "CalibrateAngles",
			  veVariantUn32(&val, 0), &veUnitNone);

	/* Angle items will be created on-demand when calculations are enabled */
}

static float total_acceleration(float x, float y, float z)
{
	return sqrtf(x * x + y * y + z * z);
}

static float angle_from_component(float component, float total)
{
	if (total == 0)
		return 0;

	/* Calculate angle and convert to degrees, centered around 0 */
	return (acosf(component / total) * 180.0f / M_PI) - 90.0f;
}

static void clear_angle_items(struct VeItem *root)
{
	struct VeItem *item;

	item = veItemByUid(root, "AngleX");
	if (item)
		veItemDeleteBranch(item);

	item = veItemByUid(root, "AngleY");
	if (item)
		veItemDeleteBranch(item);

	item = veItemByUid(root, "AngleZ");
	if (item)
		veItemDeleteBranch(item);
}

static void create_angle_items(struct VeItem *root)
{
	VeVariant val;

	/* Only create items if they don't exist yet */
	if (!veItemByUid(root, "AngleX"))
		ble_dbus_set_item(root, "AngleX",
				  veVariantInvalidType(&val, VE_FLOAT),
				  &veUnitDegree);

	if (!veItemByUid(root, "AngleY"))
		ble_dbus_set_item(root, "AngleY",
				  veVariantInvalidType(&val, VE_FLOAT),
				  &veUnitDegree);

	if (!veItemByUid(root, "AngleZ"))
		ble_dbus_set_item(root, "AngleZ",
				  veVariantInvalidType(&val, VE_FLOAT),
				  &veUnitDegree);
}

static int handle_calibration(struct VeItem *root, float x, float y, float z)
{
	struct VeItem *item;
	VeVariant val;
	int calibrate;

	calibrate = veItemValueInt(root, "CalibrateAngles");
	if (calibrate != 1)
		return 0;

	/* Store calibration offsets */
	item = veItemByUid(root, "CalibX");
	if (item)
		veItemOwnerSet(item, veVariantFloat(&val, -x));

	item = veItemByUid(root, "CalibY");
	if (item)
		veItemOwnerSet(item, veVariantFloat(&val, -y));

	item = veItemByUid(root, "CalibZ");
	if (item)
		veItemOwnerSet(item, veVariantFloat(&val, 1.0f - z));

	/* Reset calibration flag */
	ble_dbus_set_int(root, "CalibrateAngles", 0);

	return 1;
}

void angle_calculate(struct VeItem *root)
{
	struct VeItem *accel_x, *accel_y, *accel_z;
	VeVariant val_x, val_y, val_z, val;
	float x, y, z;
	float calib_x, calib_y, calib_z;
	float total;
	float angle_x, angle_y, angle_z;
	int calculate;

	if (!root)
		return;

	calculate = veItemValueInt(root, "CalculateAngles");

	/* Get acceleration data */
	accel_x = veItemByUid(root, "AccelX");
	accel_y = veItemByUid(root, "AccelY");
	accel_z = veItemByUid(root, "AccelZ");

	if (!accel_x || !accel_y || !accel_z) {
		if (calculate)
			clear_angle_items(root);
		return;
	}

	if (!veItemIsValid(accel_x) || !veItemIsValid(accel_y) ||
	    !veItemIsValid(accel_z)) {
		if (calculate)
			clear_angle_items(root);
		return;
	}

	veItemLocalValue(accel_x, &val_x);
	veItemLocalValue(accel_y, &val_y);
	veItemLocalValue(accel_z, &val_z);

	veVariantToFloat(&val_x);
	veVariantToFloat(&val_y);
	veVariantToFloat(&val_z);

	x = val_x.value.Float;
	y = val_y.value.Float;
	z = val_z.value.Float;

	/* Handle calibration first */
	if (handle_calibration(root, x, y, z)) {
		clear_angle_items(root);
		return;
	}

	/* If calculations are disabled, remove angle items */
	if (!calculate) {
		clear_angle_items(root);
		return;
	}

	/* Create angle items if needed */
	create_angle_items(root);

	/* Get calibration offsets */
	calib_x = veItemValueFloat(root, "CalibX");
	calib_y = veItemValueFloat(root, "CalibY");
	calib_z = veItemValueFloat(root, "CalibZ");

	/* Apply calibration */
	x += calib_x;
	y += calib_y;
	z += calib_z;

	/* Calculate total acceleration */
	total = total_acceleration(x, y, z);
	if (total == 0) {
		veItemInvalidate(veItemByUid(root, "AngleX"));
		veItemInvalidate(veItemByUid(root, "AngleY"));
		veItemInvalidate(veItemByUid(root, "AngleZ"));
		return;
	}

	/* Calculate angles and round to whole degrees */
	angle_x = roundf(angle_from_component(x, total));
	angle_y = roundf(angle_from_component(y, total));
	angle_z = roundf(angle_from_component(z, total));

	/* Update angle items */
	ble_dbus_set_item(root, "AngleX", veVariantFloat(&val, angle_x),
			  &veUnitDegree);
	ble_dbus_set_item(root, "AngleY", veVariantFloat(&val, angle_y),
			  &veUnitDegree);
	ble_dbus_set_item(root, "AngleZ", veVariantFloat(&val, angle_z),
			  &veUnitDegree);
}

static void angle_setting_changed(struct VeItem *root, struct VeItem *setting,
				  const void *data)
{
	angle_calculate(root);
	veItemSendPendingChanges(root);
}
