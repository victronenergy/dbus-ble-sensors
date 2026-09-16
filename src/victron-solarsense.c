#include "victron-solarsense.h"

#include <ble-dbus.h>
#include <formatters.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>

static const struct reg_info solarsense_adv[] = {
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset	= 0 / 8,
		.shift	= 0 % 8,
		.name	= "Alarms/RawValue",
		.format	= &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 0,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/LowVoltage",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 2,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/HighVoltage",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 10,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/LowTemperature",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 12,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/HighTemperature",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 26,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/IncompleteConfig",
		.format = &veUnitNone,
	},
	{
		// Charger error - VE_REG_CHR_ERROR_CODE
		.type	= VE_UN8,
		.offset	= 32 / 8,
		.shift	= 32 % 8,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ErrorCode",
		.formatter = formatters_charger_error,
	},
	{
		// Installation Power - VE_REG_DC_INPUT_POWER
		.type	= VE_UN32,
		.offset	= 40 / 8,
		.shift	= 40 % 8,
		.scale	= 1,
		.bits	= 20,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "InstallationPower",
		.format	= &veUnitWatt,
	},
	{
		// Todays Yield - VE_REG_CHR_TODAY_YIELD
		.type	= VE_UN32,
		.offset	= 60 / 8,
		.shift	= 60 % 8,
		.scale	= 100,
		.bits	= 20,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "TodaysYield",
		.format	= &veUnitKiloWattHour,
	},
	{
		// Irradiance - VE_REG_IRRADIANCE
		.type	= VE_UN16,
		.offset	= 80 / 8,
		.shift	= 80 % 8,
		.bits	= 14,
		.scale	= 10,
		.inval	= 0x3fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Irradiance",
		.format	= &veUnitIrradiance1Dec,
	},
	{
		// Panel Temperature - VE_REG_INTERNAL_TEMPERATURE
		.type	= VE_UN16,
		.offset	= 94 / 8,
		.shift	= 94 % 8,
		.bits	= 11,
		.scale	= 10,
		.bias	= -60,
		.inval	= 0x7ff,
		.flags	= REG_FLAG_INVALID,
		.name	= "CellTemperature",
		.format	= &veUnitCelsius1Dec,
	},
};

static const struct dev_info solarsense_dev_info = {
	.unknown_name	= "Unknown SolarSense",
	.role		= "meteo",
	.num_regs	= array_size(solarsense_adv),
	.regs		= solarsense_adv,
};

const struct victron_device solarsense_victron_device = {
	.dev_info	= &solarsense_dev_info,
	.def_name	= "SolarSense",
};
