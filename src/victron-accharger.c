#include "victron-accharger.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static const struct reg_info accharger_adv[] = {
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
		// Battery Voltage 1 - VE_REG_DC_CHANNEL1_VOLTAGE
		.type   = VE_UN16,
		.offset = 16 / 8,
		.shift  = 16 % 8,
		.bits   = 13,
		.inval  = 0x1fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Voltage",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// Battery Current 1 - VE_REG_DC_CHANNEL1_CURRENT
		.type   = VE_UN16,
		.offset = 29 / 8,
		.shift  = 29 % 8,
		.bits   = 11,
		.inval  = 0x7ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Current",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
	{
		// Battery Voltage 2 - VE_REG_DC_CHANNEL2_VOLTAGE
		.type   = VE_UN16,
		.offset = 40 / 8,
		.shift  = 40 % 8,
		.bits   = 13,
		.inval  = 0x1fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/1/Voltage",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// Battery Current 2 - VE_REG_DC_CHANNEL2_CURRENT
		.type   = VE_UN16,
		.offset = 53 / 8,
		.shift  = 53 % 8,
		.bits   = 11,
		.inval  = 0x7ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/1/Current",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
	{
		// Battery Voltage 3 - VE_REG_DC_CHANNEL3_VOLTAGE
		.type   = VE_UN16,
		.offset = 64 / 8,
		.shift  = 64 % 8,
		.bits   = 13,
		.inval  = 0x1fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/2/Voltage",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// Battery Current 3 - VE_REG_DC_CHANNEL3_CURRENT
		.type   = VE_UN16,
		.offset = 77 / 8,
		.shift  = 77 % 8,
		.bits   = 11,
		.inval  = 0x7ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/2/Current",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
	{
		// Temperature - VE_REG_BAT_TEMPERATURE
		.type   = VE_UN8,
		.offset = 88 / 8,
		.shift  = 88 % 8,
		.bits   = 7,
		.inval  = 0x7f,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Temperature",
		.scale  = 1,
		.bias   = -40,
		.format = &veUnitCelsius0Dec,
	},
	{
		// AC current - VE_REG_AC_ACTIVE_INPUT_L1_CURRENT
		.type   = VE_UN16,
		.offset = 95 / 8,
		.shift  = 95 % 8,
		.bits   = 9,
		.inval  = 0x1ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Ac/In/L1/I",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
};

static const struct dev_info accharger_dev_info = {
	.role             = "charger",
	.unknown_name	  = "Unknown AC Charger",
	.num_regs         = array_size(accharger_adv),
	.regs             = accharger_adv,
};

const struct victron_device accharger_victron_device = {
	.dev_info = &accharger_dev_info,
	.def_name = "AC Charger",
};
