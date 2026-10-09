#include "victron-bsc.h"

#include <ble-dbus.h>
#include <formatters.h>

#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

veBool bsc_is_supported(const uint8_t *buf, int len)
{
	uint16_t product_id;

	if (len < 4)
		return veFalse;

	product_id = bt_get_le16(&buf[2]);
	return ((product_id >= VE_PROD_ID_BSC && product_id <= 0xA33F)
		|| (product_id >= 0xA360 && product_id <= 0xA37F))
		/* The following products have a VE.Direct port so we will not
		 * support instant readout for them. */
		&& product_id != VE_PROD_ID_BSC_IP22_12_40_1
		&& product_id != VE_PROD_ID_BSC_IP22_12_40_3;
}

static const struct reg_info bsc_adv[] = {
	{
		.type		= VE_UN8,
		.offset		= 0 / 8,
		.shift		= 0 % 8,
		.inval		= 0xff,
		.flags		= REG_FLAG_INVALID,
		.name		= "State",
		.formatter	= formatters_state,
	},
	{
		.type		= VE_UN8,
		.offset		= 8 / 8,
		.shift		= 8 % 8,
		.inval		= 0xff,
		.flags		= REG_FLAG_INVALID,
		.name		= "ErrorCode",
		.formatter	= formatters_charger_error,
	},
	{
		.type		= VE_UN16,
		.offset		= 16 / 8,
		.shift		= 16 % 8,
		.bits		= 13,
		.scale		= 100,
		.inval		= 0x1fff,
		.flags		= REG_FLAG_INVALID,
		.name		= "Dc/0/Voltage",
		.format		= &veUnitVolt2Dec,
	},
	{
		.type		= VE_UN16,
		.offset		= 29 / 8,
		.shift		= 29 % 8,
		.bits		= 11,
		.scale		= 10,
		.inval		= 0x7ff,
		.flags		= REG_FLAG_INVALID,
		.name		= "Dc/0/Current",
		.format		= &veUnitAmps1Dec,
	},
	{
		.type		= VE_UN8,
		.offset		= 88 / 8,
		.shift		= 88 % 8,
		.bits		= 7,
		.scale		= 1,
		.bias		= -40,
		.inval		= 0x7f,
		.flags		= REG_FLAG_INVALID,
		.name		= "Dc/0/Temperature",
		.format		= &veUnitCelsius1Dec,
	},
	{
		.type		= VE_UN16,
		.offset		= 95 / 8,
		.shift		= 95 % 8,
		.bits		= 9,
		.scale		= 10,
		.inval		= 0x1ff,
		.flags		= REG_FLAG_INVALID,
		.name		= "Ac/In/L1/I",
		.format		= &veUnitAmps1Dec,
	},
};

static const struct dev_info bsc_dev_info = {
	.unknown_name	= "Unknown Blue Smart Charger",
	.role		= "charger",
	.num_regs	= array_size(bsc_adv),
	.regs		= bsc_adv,
};

const struct victron_device bsc_victron_device = {
	.dev_info	= &bsc_dev_info,
	.def_name	= "Blue Smart Charger",
};
