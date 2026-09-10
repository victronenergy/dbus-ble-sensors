#include "victron-solarsense.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>

static const struct reg_info solarsense_adv[] = {
	{
		.type	= VE_UN32,
		.offset	= 0 / 8,
		.shift	= 0 % 8,
		.name	= "ErrorCode",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 32 / 8,
		.shift	= 32 % 8,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ChrErrorCode",
		.format	= &veUnitNone,
	},
	{
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
