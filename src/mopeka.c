#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "mopeka.h"
#include "task.h"

static struct VeSettingProperties capacity_setting_props = {
    .type		     = VE_FLOAT,
    .def.value.Float = 0.0318226f,
    .min.value.Float = 0.0f,
    .max.value.Float = 9.9999f,
};

static struct VeSettingProperties sense_type_setting_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 2,
	.min.value.SN32	= 1,
	.max.value.SN32	= 2,
};

static struct VeSettingProperties standard_setting_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 2,
	.min.value.SN32	= 0,
	.max.value.SN32	= 2,
};

static struct VeSettingProperties rawValueFull = {
        .type        = VE_FLOAT,
        .def.value.Float= 38.3f,
        .min.value.Float= 0.0f,
        .max.value.Float= 2000.0f,
};

static struct VeSettingProperties rawValueEmpty = {
        .type        = VE_FLOAT,
        .def.value.Float= 0.3f,
        .min.value.Float= 0.0f,
        .max.value.Float= 100.0f,
};

static struct VeSettingProperties fluid_type_setting_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 0,
	.min.value.SN32	= 0,
	.max.value.SN32	= 6,
};

static struct VeSettingProperties volume_unit_setting_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 0,
	.min.value.SN32	= 0,
	.max.value.SN32	= 9999,
};

static const struct dev_setting mopeka_settings[] = {
	{
		.name	= "Capacity",
		.props	= &capacity_setting_props,
	},
	{
		.name	= "SenseType",
		.props	= &sense_type_setting_props,
	},
	{
		.name	= "Standard",
		.props	= &standard_setting_props,
	},
    {
        .name    = "RawValueFull",
        .props    = &rawValueFull,
    },
    {
        .name    = "RawValueEmpty",
        .props    = &rawValueEmpty,
    },
	{
		.name	= "FluidType",
		.props	= &fluid_type_setting_props,
	},
	{
		.name	= "VolumeUnit",
		.props	= &volume_unit_setting_props,
	},
};

static const struct dev_info mopeka_sensor = {
	.product_id	= VE_PROD_ID_MOPEKA_SENSOR,
	.dev_instance	= 20,
	.dev_class	= "analog",
	.dev_prefix	= "mopeka_",
	.role		= "tank",
	.num_settings	= array_size(mopeka_settings),
	.settings	= mopeka_settings,
};

static const VeVariantUnitFmt veUnitmm = { 0, "mm" };

static const struct reg_info mopeka_raw[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
		.flags	= REG_FLAG_INVALID,
		.name	= "MfgSensorTypeId",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 1,
        .mask   = 0x7f,
        .scale  = 32,
        .flags  = REG_FLAG_BIG_ENDIAN,
        .name   = "BatteryVoltage",
        .format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.mask   = 254,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},	{
		.type	= VE_UN8,
		.offset	= 2,
		.mask   = 1,
		.name	= "SyncButton",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.mask   = 16383,
		.name	= "TankLevelInMM",
		.format	= &veUnitmm,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.mask   = 3,
		.name	= "Confidence",
		.format	= &veUnitNone,
	},
};

// Returns the measured fluid height, in mm
// scanData - represents the array of raw bytes for the manufacturing data
static int get_tank_level_in_mm(int mfg_sensor_type_id, int tank_level, float temperature)
{		
	double coef[3];

	if (mfg_sensor_type_id == 3) {
	    coef[0] = 0.573045;
        coef[1] = -0.002822;
        coef[2] = -0.00000535;
	} else {
	    // For H20 (water) use the following coefficients instead
	    coef[0] = 0.600592;
		coef[1] = -0.003124; 
		coef[2] = -0.00001368;
	}
    // For NH3 (Ammonia) use the following coefficients instead
    // const double coef[3] = { 0.906410, -0.003398, -0.00000299 };

    // Apply 2nd order polynomial to compensating the raw into mm of LPG
    float mm = (tank_level * (coef[0] + coef[1] * temperature + coef[2] * temperature * temperature));
    return (int)mm;
}

static float get_tank_full_percentage(int tank_level_in_mm, int tank_height_in_mm)
{
	float percent_full = ((tank_level_in_mm * 1.0f)/ (tank_height_in_mm * 1.0f) * 100);
	return percent_full;
}

// Arbitrary scaling of battery voltage to percent for CR2032
float get_battery_percentage(float batv)
{
    float percent = (batv - 2.2f) / 0.65f * 100.0f;
    if (percent < 0.0f) { return 0; }
	if (percent > 100.0f) { return 100; }
    return percent;
}

static void mopeka_update_status(const char *dev_uid, const char *dev_name)
{
	struct VeItem *droot;
	struct VeItem *status_item;
	struct VeItem *battery_voltage;
	struct VeItem *temperature;
	struct VeItem *tank_level;
	struct VeItem *tank_height_in_mm_item;
	struct VeItem *tank_percent_full;
	struct VeItem *capacity_item;
	struct VeItem *remaining_item;

	VeVariant val;
	VeVariant v_battery_voltage;
	float level_percent = 0.0f;

	droot = ble_dbus_get_dev(dev_uid);
	if (!droot)
		return;

	status_item = veItemByUid(droot, "Status");
	if (!status_item)
		return;
	veItemOwnerSet(status_item, veVariantUn32(&val, STATUS_OK));


	// Set the MfgSensorType to a user-friendly description of the manufacturer's sensor version
	int mfg_sensor_type_id = veItemValueInt(droot, "MfgSensorTypeId");
	mfg_sensor_type_id == 3 ? veVariantStr(&val, "Mopeka Pro LPG Sensor") : veVariantStr(&val, "Mopeka Pro Water Sensor");
	veItemOwnerSet(veItemGetOrCreateUid(droot, "MfgSensorType"), &val);

	
	// Set some values so that the GUI behaves properly
	veItemOwnerSet(veItemGetOrCreateUid(droot, "SenseType"), veVariantUn32(&val, -1));
	veItemOwnerSet(veItemGetOrCreateUid(droot, "Standard"), veVariantUn32(&val, 2));
	veItemOwnerSet(veItemGetOrCreateUid(droot, "RawValueEmpty"), veVariantInvalidType(&val, VE_UN32));
	veItemOwnerSet(veItemGetOrCreateUid(droot, "RawUnit"), veVariantStr(&val, "mm"));


	//  Set Temperature
	temperature = veItemByUid(droot, "Temperature");
	int temp = veItemValueInt(droot, "Temperature") - 40;
	veVariantUn16(&val, temp);
	veItemOwnerSet(temperature, &val);


	//  Set BatteryPercent based on BatteryVoltage
	battery_voltage = veItemByUid(droot, "BatteryVoltage");
	veItemValue(battery_voltage, &v_battery_voltage);


	// Update TankLevelInMM based on the Mopeka sensor version, 
	tank_level = veItemByUid(droot, "TankLevelInMM");
	int level_in_mm = get_tank_level_in_mm(mfg_sensor_type_id, veItemValueInt(droot, "TankLevelInMM"), temp);
	veVariantUn16(&val, level_in_mm);
	veItemOwnerSet(tank_level, &val);


	// Set the Level parameter to the percent full
	// Because Level doesn't get parsed from the BLE advertisement, it may not exist yet.  We need to use 
	// veItemGetOrCreateUid() and set the format so it'll be created if it doesn't exist.
	tank_percent_full = veItemGetOrCreateUid(droot, "Level");
	veItemSetFmt(tank_percent_full, veVariantFmt, &veUnitNone);
	tank_height_in_mm_item = veItemGetOrCreateUid(droot, "RawValueFull"); 
	veItemValue(tank_height_in_mm_item, &val);
	int tank_height_in_mm = (int)val.value.Float;
	if (tank_height_in_mm <= 0)
	{
		veVariantFloat(&val, 300.0);
		veItemOwnerSet(tank_height_in_mm_item, &val);		
		tank_height_in_mm = 300;
	} 
	level_percent = get_tank_full_percentage(level_in_mm, tank_height_in_mm);
	veVariantFloat(&val, level_percent);
	veItemOwnerSet(tank_percent_full, &val);

	// Set the Remaining parameter based on percent full
	capacity_item = veItemGetOrCreateUid(droot, "Capacity"); 
	veItemValue(capacity_item, &val);
	float capacity_value = val.value.Float;
	float remaining = (capacity_value * level_percent / 100);
	if (capacity_value > 0)
	{
		veVariantFloat(&val, remaining);
		// Remaining may not exist yet so use veItemGetOrCreateUid() to create it if needed.
		remaining_item = veItemGetOrCreateUid(droot, "Remaining"); 
		veItemOwnerSet(remaining_item, &val);
	}

	// veItemValueFmtString()
	// logE("ROB"," ======================================");
	// logE("ROB"," |   MfgSensorTypeId \t%d",     veItemValueInt(droot, "MfgSensorTypeId"));
	// // logE("ROB"," |   MfgSensorType \t%d",     veItemValueStr(droot, "MfgSensorType"));
	// logE("ROB"," |   BatteryVoltage \t\t%.2f",  v_battery_voltage.value.Float);
	// logE("ROB"," |   Temperature \t\t%d",       veItemValueInt(droot, "Temperature"));
	// logE("ROB"," |   LevelInMM \t\t%d",         veItemValueInt(droot, "TankLevelInMM"));
	// logE("ROB"," |   RawValueFull \t\t%d",      veItemValueInt(droot, "RawValueFull"));
	// logE("ROB"," |   Level % \t\t%d",           veItemValueInt(droot, "Level"));
	// logE("ROB"," |   Remaining \t\t%.8f",       val.value.Float);
	// logE("ROB"," |   Confidence \t\t%d",        veItemValueInt(droot, "Confidence"));
	// logE("ROB"," |   SyncButton \t\t%d",        veItemValueInt(droot, "SyncButton"));
	// logE("ROB"," ======================================");
}

int mopeka_handle_mfg(const uint8_t *data, int len)
{
	char dev_name[16];
	char dev_uid[16];

	snprintf(dev_name, sizeof(dev_name), "Mopeka %02X:%02X", data[6], data[7]);
	snprintf(dev_uid, sizeof(dev_uid), "%02x%02x%02x", data[5], data[6], data[7]);

	// logE("ROB","------------------------------------------------------------------------------------------");
	// logE("ROB","Parsing advertisement for %s :  0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",dev_name, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

	// Only pay attention to Mopeka's two 'bottom-up' sensors
	int mfg_sensor_type_id = data[0];
	if (mfg_sensor_type_id != 3 && mfg_sensor_type_id != 5) return false;

	// Parse the manufacturer's data from the advertisement into dbus items as defined by the mopeka_raw struct
	ble_dbus_set_regs(dev_uid, &mopeka_sensor, mopeka_raw, array_size(mopeka_raw), data, len);

	// Set the device's name
	ble_dbus_set_name(dev_uid, dev_name);

	// Do postprocessing on the data parsed from the advertisement and push various calculated values back to the device's DBus item
	mopeka_update_status(dev_uid, dev_name);

	return 0;
}

