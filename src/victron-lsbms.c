#include "victron-lsbms.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static int xlate_bms_io(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (rawval == 0) {
		veVariantUn16(val, VE_INVALID_UN16);
	} else if (rawval == 1) {
		veVariantUn16(val, 1);
	} else {
		veVariantUn16(val, 0);
	}
	return 0;
}

static const struct reg_info lsbms_adv[] = {
	{
		// Error - VE_REG_BMS_ERROR
		.type	= VE_UN8,
		.offset = 0 / 8,
		.shift	= 0 % 8,
		.name	= "ErrorCode",
		.format = &veUnitNone,
	},
	{
		// TTG - VE_REG_TTG
		.type	= VE_UN16,
		.offset = 8 / 8,
		.shift	= 8 % 8,
		.inval	= 0xffff,
		.scale	= 1.0/60,
		.flags	= REG_FLAG_INVALID,
		.name	= "TimeToGo",
		.format = &veUnitSeconds,
	},
	{
		// Battery Voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		.type	= VE_SN16,
		.offset = 24 / 8,
		.shift	= 24 % 8,
		.scale	= 100,
		.inval	= 0x7fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Voltage",
		.format = &veUnitVolt2Dec,
	},
	{
		// Battery Current - VE_REG_DC_CHANNEL1_CURRENT
		.type	= VE_SN16,
		.offset = 40 / 8,
		.shift	= 40 % 8,
		.scale	= 10,
		.inval	= 0x7fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Current",
		.format = &veUnitAmps1Dec,
	},
	{
		// IO status - VE_REG_BMS_IO
		.type	= VE_UN16,
		.offset = 56 / 8,
		.shift	= 56 % 8 + 2,
		.bits	= 2,
		.xlate	= xlate_bms_io,
		.name	= "Io/AllowToCharge",
		.format = &veUnitNone,
	},
	{
		// IO status - VE_REG_BMS_IO
		.type	= VE_UN16,
		.offset = 56 / 8,
		.shift	= 56 % 8 + 4,
		.bits	= 2,
		.xlate	= xlate_bms_io,
		.name	= "Io/AllowToDischarge",
		.format = &veUnitNone,
	},
	{
		// IO status - VE_REG_BMS_IO
		.type	= VE_UN16,
		.offset = 56 / 8,
		.shift	= 56 % 8 + 6,
		.bits	= 2,
		.xlate	= xlate_bms_io,
		.name	= "Io/ExternalRelay",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 0,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/LowCellVoltage",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 2,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/HighCurrent",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 4,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/HighInternalTemperature",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 6,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/Contactor",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 8,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/BmsCable",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 10,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/LoadDisconnect",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 12,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/LowSoc",
		.format = &veUnitNone,
	},
	{
		// Warnings/Alarms - VE_REG_BMS_WARNINGS_ALARMS
		.type	= VE_UN32,
		.offset = 72 / 8,
		.shift	= 72 % 8 + 16,
		.bits	= 2,
		.flags	= REG_FLAG_WARN_ALARM,
		.name	= "Alarms/HighTemperature",
		.format = &veUnitNone,
	},
	{
		// State of Charge
		.type	= VE_UN16,
		.offset = 90 / 8,
		.shift	= 90 % 8,
		.bits	= 10,
		.scale	= 10,
		.inval	= 0x3ff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Soc",
		.format = &veUnitPercentage1Dec,
	},
	{
		// Consumed Ah
		.type	= VE_UN32,
		.offset = 100 / 8,
		.shift	= 100 % 8,
		.bits	= 20,
		.scale	= -10,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ConsumedAmphours",
		.format = &veUnitAmpHour1Dec,
	},
	{
		// Temperature
		.type	= VE_UN8,
		.offset = 120 / 8,
		.shift	= 120 % 8,
		.bits	= 7,
		.scale	= 1,
		.bias	= -40,
		.inval	= 0x7f,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Temperature",
		.format = &veUnitCelsius0Dec,
	},
};

static const struct dev_info lsbms_dev_info = {
	.role	      = "battery",
	.unknown_name = "Unknown BMS / Battery",
	.num_regs     = array_size(lsbms_adv),
	.regs	      = lsbms_adv,
};

const struct victron_device lsbms_victron_device = {
	.dev_info = &lsbms_dev_info,
	.def_name = "LSBMS",
};
