#include "victron-battery_monitor.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/vecan/regs_payload.h>

#define ONLY_SMART_BATTERY_SENSE	1

/*
 * The auxiliary field at bit offset 48 carries either the aux (starter)
 * voltage, the mid-point voltage or the temperature, as selected by the
 * 2-bit AuxMode field (VE_REG_BMV_AUX_INPUT) at bit offset 64. The selector
 * itself is not published; instead the Settings/Has* booleans derived from
 * it indicate which value is present.
 */
#if !ONLY_SMART_BATTERY_SENSE
static int xlate_aux_voltage(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (veItemValueInt(root, "Settings/HasStarterVoltage") != 1)
		return -1;

	veVariantFloat(val, (int16_t)rawval / 100.0f);

	return 0;
}

static int xlate_mid_voltage(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (veItemValueInt(root, "Settings/HasMidVoltage") != 1)
		return -1;

	veVariantFloat(val, (uint16_t)rawval / 100.0f);

	return 0;
}
#endif

static int xlate_temperature(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (veItemValueInt(root, "Settings/HasTemperature") != 1)
		return -1;

	veVariantFloat(val, (uint16_t)rawval / 100.0f - 273.15f);

	return 0;
}

static int xlate_has_starter_voltage(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	VE_UNUSED(root);

	veVariantUn32(val, rawval == VE_REG_BMV_AUX_INPUT_AUX_VOLTAGE);

	return 0;
}

static int xlate_has_mid_voltage(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	VE_UNUSED(root);

	veVariantUn32(val, rawval == VE_REG_BMV_AUX_INPUT_MID_VOLTAGE);

	return 0;
}

static int xlate_has_temperature(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	VE_UNUSED(root);

	veVariantUn32(val, rawval == VE_REG_BMV_AUX_INPUT_TEMPERATURE);

	return 0;
}

static const struct reg_info battery_monitor_adv[] = {
	{
		// Battery voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		.type	= VE_SN16,
		.offset	= 16 / 8,
		.shift	= 16 % 8,
		.scale	= 100,
		.inval	= 0x7fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Voltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		// Aux input selector - VE_REG_BMV_AUX_INPUT
		.type	= VE_UN8,
		.offset	= 64 / 8,
		.shift	= 64 % 8,
		.bits	= 2,
		.xlate	= xlate_has_starter_voltage,
		.name	= "Settings/HasStarterVoltage",
		.format	= &veUnitNone,
	},
	{
		// Aux input selector - VE_REG_BMV_AUX_INPUT
		.type	= VE_UN8,
		.offset	= 64 / 8,
		.shift	= 64 % 8,
		.bits	= 2,
		.xlate	= xlate_has_mid_voltage,
		.name	= "Settings/HasMidVoltage",
		.format	= &veUnitNone,
	},
	{
		// Aux input selector - VE_REG_BMV_AUX_INPUT
		.type	= VE_UN8,
		.offset	= 64 / 8,
		.shift	= 64 % 8,
		.bits	= 2,
		.xlate	= xlate_has_temperature,
		.name	= "Settings/HasTemperature",
		.format	= &veUnitNone,
	},
	{
		// Temperature - VE_REG_BAT_TEMPERATURE
		.type	= VE_UN16,
		.offset	= 48 / 8,
		.shift	= 48 % 8,
		.xlate	= xlate_temperature,
		.name	= "Dc/0/Temperature",
		.format	= &veUnitCelsius1Dec,
	},
#if !ONLY_SMART_BATTERY_SENSE
	{
		// TTG - VE_REG_TTG
		.type	= VE_UN16,
		.offset	= 0 / 8,
		.shift	= 0 % 8,
		.inval	= 0xffff,
		.scale	= 1.0/60,
		.flags	= REG_FLAG_INVALID,
		.name	= "TimeToGo",
		.format	= &veUnitSeconds,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_LOW_VOLTAGE
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 0,
		.bits	= 1,
		.name	= "Alarms/LowVoltage",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_HIGH_VOLTAGE
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 1,
		.bits	= 1,
		.name	= "Alarms/HighVoltage",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_LOW_SOC
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 2,
		.bits	= 1,
		.name	= "Alarms/LowSoc",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_LOW_VOLTAGE2 (aux/starter battery)
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 3,
		.bits	= 1,
		.name	= "Alarms/LowStarterVoltage",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_HIGH_VOLTAGE2 (aux/starter battery)
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 4,
		.bits	= 1,
		.name	= "Alarms/HighStarterVoltage",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_LOW_TEMPERATURE
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 5,
		.bits	= 1,
		.name	= "Alarms/LowTemperature",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_HIGH_TEMPERATURE
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 6,
		.bits	= 1,
		.name	= "Alarms/HighTemperature",
		.format	= &veUnitNone,
	},
	{
		// Alarm reason - VE_REG_ALARM_REASON_MID_VOLTAGE
		.type	= VE_UN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8 + 7,
		.bits	= 1,
		.name	= "Alarms/MidVoltage",
		.format	= &veUnitNone,
	},
	{
		// Battery current - VE_REG_DC_CHANNEL1_CURRENT_MA
		.type	= VE_SN32,
		.offset	= 66 / 8,
		.shift	= 66 % 8,
		.bits	= 22,
		.scale	= 1000,
		.inval	= 0x1fffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Current",
		.format	= &veUnitAmps1Dec,
	},
	{
		// Aux voltage - VE_REG_DC_CHANNEL2_VOLTAGE
		.type	= VE_UN16,
		.offset	= 48 / 8,
		.shift	= 48 % 8,
		.xlate	= xlate_aux_voltage,
		.name	= "Dc/1/Voltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		// Mid voltage - VE_REG_BATTERY_MID_POINT_VOLTAGE
		.type	= VE_UN16,
		.offset	= 48 / 8,
		.shift	= 48 % 8,
		.xlate	= xlate_mid_voltage,
		.name	= "Dc/0/MidVoltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		// Consumed Ah - VE_REG_CAH
		.type	= VE_UN32,
		.offset	= 88 / 8,
		.shift	= 88 % 8,
		.bits	= 20,
		.scale	= -10,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ConsumedAmphours",
		.format	= &veUnitAmpHour1Dec,
	},
	{
		// State of Charge - VE_REG_SOC
		.type	= VE_UN16,
		.offset	= 108 / 8,
		.shift	= 108 % 8,
		.bits	= 10,
		.scale	= 10,
		.inval	= 0x3ff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Soc",
		.format	= &veUnitPercentage1Dec,
	},
#endif	
};

static const struct dev_info battery_monitor_dev_info = {
	.role	      = "battery",
	.unknown_name = "Unknown Battery Monitor",
	.num_regs     = array_size(battery_monitor_adv),
	.regs	      = battery_monitor_adv,
};

const struct victron_device battery_monitor_victron_device = {
	.dev_info = &battery_monitor_dev_info,
	.def_name = "Battery Monitor",
};

veBool battery_monitor_is_supported(const uint8_t *buf, int len)
{
#if ONLY_SMART_BATTERY_SENSE
	uint16_t product_id = bt_get_le16(&buf[2]);
	return product_id == VE_PROD_ID_VT_SENSOR_SMART
		|| product_id == VE_PROD_ID_VT_SENSOR_SMART_S132V7;
#else
	return veTrue;
#endif
}
