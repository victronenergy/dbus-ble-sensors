#include "victron-inverter.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static const struct reg_info inverter_adv[] = {
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
		// Alarm reason - VE_REG_ALARM_REASON
		.type   = VE_UN16,
		.offset = 8 / 8,
		.shift  = 8 % 8,
		.name   = "AlarmReason",
		.format = &veUnitNone,
	},
	{
		// Battery voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		// 16-bit signed, range -327.68 to 327.66V, scale 0.01V
		.type   = VE_SN16,
		.offset = 24 / 8,
		.shift  = 24 % 8,
		.inval  = 0x7fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Dc/0/Voltage",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// AC apparent power - VE_REG_AC_OUT_APPARENT_POWER
		.type   = VE_UN16,
		.offset = 40 / 8,
		.shift  = 40 % 8,
		.inval  = 0xffff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Ac/Out/L1/S",
		.scale  = 1,
		.format = &veUnitVA,
	},
	{
		// AC voltage - VE_REG_AC_OUT_VOLTAGE
		// 15-bit unsigned, range 0 to 327.66V, scale 0.01V
		.type   = VE_UN16,
		.offset = 56 / 8,
		.shift  = 56 % 8,
		.bits   = 15,
		.inval  = 0x7fff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Ac/Out/L1/V",
		.scale  = 100,
		.format = &veUnitVolt2Dec,
	},
	{
		// AC current - VE_REG_AC_OUT_CURRENT
		// 11-bit unsigned, range 0 to 204.6A, scale 0.1A
		.type   = VE_UN16,
		.offset = 71 / 8,
		.shift  = 71 % 8,
		.bits   = 11,
		.inval  = 0x7ff,
		.flags  = REG_FLAG_INVALID,
		.name   = "Ac/Out/L1/I",
		.scale  = 10,
		.format = &veUnitAmps1Dec,
	},
};

static const struct dev_info inverter_dev_info = {
	.role             = "inverter",
	.unknown_name     = "Unknown Inverter",
	.num_regs         = array_size(inverter_adv),
	.regs             = inverter_adv,
};

const struct victron_device inverter_victron_device = {
	.dev_info = &inverter_dev_info,
	.def_name = "Inverter",
};