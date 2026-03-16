#include "victron-solarsense.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>

static int solarsense_xlate_txpower(struct VeItem *root, VeVariant *val,
				    uint64_t rawval)
{
	int txp = rawval ? 6 : 0;

	veVariantUn8(val, txp);

	return 0;
}

static int solarsense_xlate_tss(struct VeItem *root, VeVariant *val,
				uint64_t rawval)
{
	int tss;

	if (rawval <= 29)
		tss = rawval * 2;
	else if (rawval <= 95)
		tss = 60 + 10 * (rawval - 30);
	else if (rawval <= 126)
		tss = 720 + 30 * (rawval - 96);
	else
		tss = rawval;

	veVariantUn16(val, tss);

	return 0;
}

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
	{
		.type	= VE_UN8,
		.offset	= 105 / 8,
		.shift	= 105 % 8,
		.bits	= 1,
		.name	= "UnspecifiedRemnant",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 106 / 8,
		.shift	= 106 % 8,
		.bits	= 8,
		.scale	= 100,
		.bias	= 1.7,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 114 / 8,
		.shift	= 114 % 8,
		.bits	= 1,
		.xlate	= solarsense_xlate_txpower,
		.name	= "TxPowerLevel",
		.format	= &veUnitdBm,
	},
	{
		.type	= VE_UN16,
		.offset	= 115 / 8,
		.shift	= 115 % 8,
		.bits	= 7,
		.inval	= 0x7f,
		.flags	= REG_FLAG_INVALID,
		.xlate	= solarsense_xlate_tss,
		.name	= "TimeSinceLastSun",
		.format	= &veUnitMinutes,
	},
};

static const struct alarm solarsense_alarms[] = {
	{
		.name	= "LowBattery",
		.item	= "BatteryVoltage",
		.level	= 3.2,
		.hyst	= 0.4,
	},
};

static const struct dev_info solarsense_dev_info = {
	.role		= "meteo",
	.num_regs	= array_size(solarsense_adv),
	.regs		= solarsense_adv,
	.num_alarms	= array_size(solarsense_alarms),
	.alarms		= solarsense_alarms,
};

const struct victron_device solarsense_victron_device = {
	.dev_info	= &solarsense_dev_info,
	.def_name	= "SolarSense",
};
