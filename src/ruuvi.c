#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include <math.h>
#include <string.h>

#include "ble-dbus.h"
#include "ruuvi.h"

static struct VeSettingProperties temp_type = {
	.type		= VE_SN32,
	.def.value.SN32	= 2,
	.min.value.SN32	= 0,
	.max.value.SN32	= 6,
};

static struct VeSettingProperties calculate_angle = {
	.type		= VE_SN32,
	.def.value.SN32	= 0,
	.min.value.SN32	= 0,
	.max.value.SN32	= 1,
};

static struct VeSettingProperties calibrate_angle = {
	.type 		= VE_SN32,
	.def.value.SN32	= 0,
	.min.value.SN32	= 0,
	.max.value.SN32	= 1,
};

static struct VeSettingProperties angle_value = {
    .type             = VE_FLOAT,
    .def.value.Float  = 0.0,
    .min.value.Float  = -180.0,
    .max.value.Float  = 180.0,
};

static const struct dev_setting ruuvi_settings[] = {
	{
		.name	= "TemperatureType",
		.props	= &temp_type,
	},
	{
		.name   = "CalculateAngles",
		.props  = &calculate_angle,
	},
	{
		.name   = "CalibrateAngles",
		.props  = &calibrate_angle,
	},
	    {
        .name   = "AngleX",
        .props  = &angle_value,
    },
    {
        .name   = "AngleY",
        .props  = &angle_value,
    },
    {
        .name   = "AngleZ",
        .props  = &angle_value,
    },
};

struct angle_state {
	int initialized;
	float calib_x;
	float calib_y;
	float calib_z;
};

static const struct dev_info ruuvi_tag = {
	.product_id	= VE_PROD_ID_RUUVI_TAG,
	.dev_instance	= 20,
	.dev_prefix	= "ruuvi_",
	.role		= "temperature",
	.num_settings	= array_size(ruuvi_settings),
	.settings	= ruuvi_settings,
};

static const struct reg_info ruuvi_rawv2[] = {
	{
		.type	= VE_SN16,
		.offset	= 1,
		.scale	= 200,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.scale	= 400,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Humidity",
		.format	= &veUnitPercentage,
	},
	{
		.type	= VE_UN16,
		.offset	= 5,
		.scale	= 100,
		.bias	= 500,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Pressure",
		.format	= &veUnitHectoPascal,
	},
	{
		.type	= VE_SN16,
		.offset	= 7,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN16,
		.offset	= 9,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN16,
		.offset	= 11,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelZ",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 13,
		.shift	= 5,
		.mask	= 0x7ff,
		.scale	= 1000,
		.bias	= 1.6,
		.inval	= 0x3ff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 13,
		.shift	= 0,
		.mask	= 0x1f,
		.scale	= 0.5,
		.bias	= -40,
		.inval	= 0x1f,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "TxPower",
		.format	= &veUnitdBm,
	},
	{
		.type	= VE_UN16,
		.offset	= 16,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "SeqNo",
		.format	= &veUnitNone,
	},
};

static void ruuvi_update_alarms(struct VeItem *devroot)
{
	struct VeItem *batv;
	struct VeItem *temp;
	struct VeItem *lowbat;

	VeVariant val;
	float low;
	int lb;

	batv = veItemByUid(devroot, "BatteryVoltage");
	if (!batv)
		return;

	temp = veItemByUid(devroot, "Temperature");
	if (!temp)
		return;

	lowbat = veItemGetOrCreateUid(devroot, "Alarms/LowBattery");
	if (!lowbat)
		return;

	veItemLocalValue(temp, &val);
	veVariantToFloat(&val);

	if (val.value.Float < -20)
		low = 2.0;
	else if (val.value.Float < 0)
		low = 2.3;
	else
		low = 2.5;

	if (veItemIsValid(lowbat)) {
		veItemLocalValue(lowbat, &val);
		veVariantToN32(&val);

		if (val.value.UN32)
			low += 0.4;
	}

	veItemLocalValue(batv, &val);
	veVariantToFloat(&val);

	if (val.value.Float < low)
		lb = 1;
	else
		lb = 0;

	veVariantUn32(&val, lb);
	veItemOwnerSet(lowbat, &val);
}

// Function to calculate total acceleration vector magnitude
static float total_acceleration(float x, float y, float z) {
    return sqrt(x * x + y * y + z * z);
}

// Function to calculate angle between vector component and axis
static float angle_between_vector_component_and_axis(float component, float length) {
    if (length == 0) return 0;
    float angle = acos(component / length);
    return (angle * 180.0 / M_PI) - 90.0; // Convert to degrees and center around 0
}

// Helper function to invalidate angle values
static void clear_angle_values(struct VeItem *root) {
    VeVariant val;
    veVariantFloat(&val, 0.0);

    ble_dbus_set_item(root, "AngleX", &val, &veUnitDegree);
    ble_dbus_set_item(root, "AngleY", &val, &veUnitDegree);
    ble_dbus_set_item(root, "AngleZ", &val, &veUnitDegree);
}

// Handle calibration
static int handle_calibration(struct VeItem *root, float x, float y, float z) {
    struct VeItem *calibrate = veItemByUid(root, "CalibrateAngles");
    if (!calibrate) {
        return 0;
    }

    VeVariant cal_val;
    veItemLocalValue(calibrate, &cal_val);

    if (!veVariantIsValid(&cal_val)) {
        return 0;
    }

    veVariantToN32(&cal_val);
    if (cal_val.value.SN32 != 1) {
        return 0;
    }

    // Store calibration values as settings
    VeVariant val;
    veVariantFloat(&val, -x);
    ble_dbus_set_item(root, "CalibX", &val, &veUnitNone);

    veVariantFloat(&val, -y);
    ble_dbus_set_item(root, "CalibY", &val, &veUnitNone);

    veVariantFloat(&val, 1.0 - z);
    ble_dbus_set_item(root, "CalibZ", &val, &veUnitNone);

    // Reset calibration flag
    ble_dbus_set_int(root, "CalibrateAngles", 0);

    // Clear angles for this update
    clear_angle_values(root);

    return 0;
}

// Main angle calculation function
static void ruuvi_calculate_angles(struct VeItem *root) {
    if (!root) {
        return;
    }

    // Get acceleration items
    struct VeItem *accel_x = veItemByUid(root, "AccelX");
    struct VeItem *accel_y = veItemByUid(root, "AccelY");
    struct VeItem *accel_z = veItemByUid(root, "AccelZ");

    if (!accel_x || !accel_y || !accel_z) {
        clear_angle_values(root);
        return;
    }

    // Get current values
    VeVariant val_x, val_y, val_z;
    veItemLocalValue(accel_x, &val_x);
    veItemLocalValue(accel_y, &val_y);
    veItemLocalValue(accel_z, &val_z);

    if (!veVariantIsValid(&val_x) || !veVariantIsValid(&val_y) || !veVariantIsValid(&val_z)) {
        clear_angle_values(root);
        return;
    }

    veVariantToFloat(&val_x);
    veVariantToFloat(&val_y);
    veVariantToFloat(&val_z);

    // Handle calibration if needed
    if (handle_calibration(root, val_x.value.Float, val_y.value.Float, val_z.value.Float)) {
        return;
    }

    // Check if calculations are enabled
    struct VeItem *calc_enabled = veItemByUid(root, "CalculateAngles");
    if (!calc_enabled) {
        clear_angle_values(root);
        return;
    }

    VeVariant calc_val;
    veItemLocalValue(calc_enabled, &calc_val);
    if (!veVariantIsValid(&calc_val)) {
        clear_angle_values(root);
        return;
    }

    veVariantToN32(&calc_val);
    if (calc_val.value.SN32 != 1) {
        clear_angle_values(root);
        return;
    }

    // Get calibration values
    float calib_x = 0, calib_y = 0, calib_z = 0;
    struct VeItem *cal_x = veItemByUid(root, "CalibX");
    struct VeItem *cal_y = veItemByUid(root, "CalibY");
    struct VeItem *cal_z = veItemByUid(root, "CalibZ");

    if (cal_x && cal_y && cal_z) {
        VeVariant cal_val_x, cal_val_y, cal_val_z;
        veItemLocalValue(cal_x, &cal_val_x);
        veItemLocalValue(cal_y, &cal_val_y);
        veItemLocalValue(cal_z, &cal_val_z);

        if (veVariantIsValid(&cal_val_x) && veVariantIsValid(&cal_val_y) && veVariantIsValid(&cal_val_z)) {
            veVariantToFloat(&cal_val_x);
            veVariantToFloat(&cal_val_y);
            veVariantToFloat(&cal_val_z);

            calib_x = cal_val_x.value.Float;
            calib_y = cal_val_y.value.Float;
            calib_z = cal_val_z.value.Float;
        }
    }

    // Apply calibration
    float accel_x_cal = val_x.value.Float + calib_x;
    float accel_y_cal = val_y.value.Float + calib_y;
    float accel_z_cal = val_z.value.Float + calib_z;

    // Calculate total acceleration
    float total = total_acceleration(accel_x_cal, accel_y_cal, accel_z_cal);
    if (total == 0) {
        clear_angle_values(root);
        return;
    }

    // Calculate angles
    float angle_x = angle_between_vector_component_and_axis(accel_x_cal, total);
    float angle_y = angle_between_vector_component_and_axis(accel_y_cal, total);
    float angle_z = angle_between_vector_component_and_axis(accel_z_cal, total);

    // Update angles in dbus
    VeVariant val;
    veVariantFloat(&val, angle_x);
    ble_dbus_set_item(root, "AngleX", &val, &veUnitDegree);

    veVariantFloat(&val, angle_y);
    ble_dbus_set_item(root, "AngleY", &val, &veUnitDegree);

    veVariantFloat(&val, angle_z);
    ble_dbus_set_item(root, "AngleZ", &val, &veUnitDegree);
}

int ruuvi_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	const uint8_t *mac = buf + 18;
	struct VeItem *root;
	char name[16];
	char dev[16];

	if (len != 24)
		return -1;

	if (buf[0] != 5)
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	root = ble_dbus_create(dev, &ruuvi_tag, NULL);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "Ruuvi %02X%02X", mac[4], mac[5]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, ruuvi_rawv2, array_size(ruuvi_rawv2), buf, len);

	ruuvi_calculate_angles(root);  // Calculate angles after setting accelerometer values
				       //
	ruuvi_update_alarms(root);
	ble_dbus_update(root);

	return 0;
}
