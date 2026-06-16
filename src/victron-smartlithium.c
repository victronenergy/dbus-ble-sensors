#include "victron-smartlithium.h"

#include <ble-dbus.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static int xlate_bms_flag_alrm_warn(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (rawval & 1) {
		veVariantUn16(val, 2);
	} else if (rawval) {
		veVariantUn16(val, 1);
	} else {
		veVariantUn16(val, 0);
	}
	return 0;
}

static const struct reg_info smartlithium_adv[] = {
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 9,
		.bits	= 2,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/HighVoltage",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 11,
		.bits	= 2,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/LowVoltage",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 15,
		.bits	= 2,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/HighTemperature",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 22,
		.bits	= 1,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/LowTemperature",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 24,
		.bits	= 1,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/InternalFailure",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 25,
		.bits	= 1,
		.name	= "Io/AllowToCharge",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 26,
		.bits	= 1,
		.name	= "Io/AllowToDischarge",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 28,
		.bits	= 1,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/Contactor",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 29,
		.bits	= 1,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/HighCurrent",
		.format = &veUnitNone,
	},
	{
		// BMS flags - VE_REG_BMS_FLAGS
		.type	= VE_UN32,
		.offset = 0 / 8,
		.shift	= 0 % 8 + 30,
		.bits	= 1,
		.xlate	= xlate_bms_flag_alrm_warn,
		.name	= "Alarms/CellImbalance",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset = 32 / 8,
		.shift	= 32 % 8,
		.bits	= 16,
		.name	= "SmartLithiumErrors",
		.format = &veUnitNone,
	},
#define CELL_VOLTAGE_ITEM(cell) \
	{ \
		.type	= VE_UN16, \
		.offset = (48 + (cell) * 7) / 8, \
		.shift	= (48 + (cell) * 7) % 8, \
		.bits	= 7, \
		.scale  = 100, \
		.bias	= 2.60f, \
		.inval  = 0x7f, \
		.flags	= REG_FLAG_INVALID, \
		.name	= "Cell/" #cell "/Voltage", \
		.format = &veUnitVolt2Dec, \
	}
	CELL_VOLTAGE_ITEM(0),
	CELL_VOLTAGE_ITEM(1),
	CELL_VOLTAGE_ITEM(2),
	CELL_VOLTAGE_ITEM(3),
	CELL_VOLTAGE_ITEM(4),
	CELL_VOLTAGE_ITEM(5),
	CELL_VOLTAGE_ITEM(6),
	CELL_VOLTAGE_ITEM(7),
	{
		// Battery Voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		.type	= VE_UN16,
		.offset = 104 / 8,
		.shift	= 104 % 8,
		.bits   = 12,
		.scale	= 100,
		.inval	= 0xfff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Voltage",
		.format = &veUnitVolt2Dec,
	},
	{
		// Balancer status - VE_REG_BALANCER_STATUS
		.type	= VE_UN8,
		.offset = 116 / 8,
		.shift	= 116 % 8,
		.bits   = 4,
		.name	= "BalancerStatus",
		.format = &veUnitNone,
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

static const struct dev_info smartlithium_dev_info = {
	.role	      = "battery",
	.unknown_name = "Unknown SmartLithium Battery",
	.num_regs     = array_size(smartlithium_adv),
	.regs	      = smartlithium_adv,
};

const struct victron_device smartlithium_victron_device = {
	.dev_info = &smartlithium_dev_info,
	.def_name = "SmartLithium",
};
