#include "victron-solarcharger.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static const struct reg_info solarcharger_adv[] = {
	{
		// Device state - VE_REG_DEVICE_STATE
		.type   = VE_UN8,
		.offset = 0 / 8,
		.shift  = 0 % 8,
		.inval  = 0xff,
		.flags  = REG_FLAG_INVALID,
		.name   = "State",
		.format = &veUnitNone,
	},
	{
		// Charger Error - VE_REG_CHR_ERROR_CODE
		.type   = VE_UN8,
		.offset = 8 / 8,
		.shift  = 8 % 8,
		.inval  = 0xff,
		.flags  = REG_FLAG_INVALID,
		.name   = "ErrorCode",
		.format = &veUnitNone,
	},
	{
		// Battery Voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		// 16-bit signed, range -327.68 to 327.66V, scale 0.01V
		.type   = VE_SN16,
		.offset = 16 / 8,
		.shift  = 16 % 8,
		.inval  = 0x7fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Voltage",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// Battery Current - VE_REG_DC_CHANNEL1_CURRENT
		// 16-bit signed, range -3276.8 to 3276.6A, scale 0.1A
		.type   = VE_SN16,
		.offset = 32 / 8,
		.shift  = 32 % 8,
		.inval  = 0x7fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Current",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
	{
		// Yield today - VE_REG_CHR_TODAY_YIELD
		// 16-bit unsigned, range 0 to 655.34kWh, scale 0.01kWh
		.type   = VE_UN16,
		.offset = 48 / 8,
		.shift  = 48 % 8,
		.inval  = 0xffff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Yield/User",
		.scale  = 100,
		.format = &veUnitKiloWattHour,
	},
	{
		// PV power - VE_REG_DC_INPUT_POWER
		// 16-bit unsigned, range 0 to 65534W, scale 1W
		.type   = VE_UN16,
		.offset = 64 / 8,
		.shift  = 64 % 8,
		.inval  = 0xffff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Yield/Power",
		.scale  = 1,
		.format = &veUnitWatt,
	},
	{
		// Load current - VE_REG_DC_OUTPUT_CURRENT
		// 9-bit unsigned, range 0 to 51.0A, scale 0.1A
		.type   = VE_UN16,
		.offset = 80 / 8,
		.shift  = 80 % 8,
		.bits   = 9,
		.inval  = 0x1ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/Out/I",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
};

static const struct dev_info solarcharger_dev_info = {
	.role             = "solarcharger",
	.unknown_name	  = "Unknown Solar Charger",
	.num_regs         = array_size(solarcharger_adv),
	.regs             = solarcharger_adv,
};

const struct victron_device solarcharger_victron_device = {
	.dev_info = &solarcharger_dev_info,
	.def_name = "Solar Charger",
};
