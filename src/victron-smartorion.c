#include "victron-smartorion.h"

#include <ble-dbus.h>
#include <formatters.h>

#include <velib/base/types.h>
#include <velib/types/variant.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

static struct VeSettingProperties output_battery_props = {
	.type		= VE_SN32,
	.def.value.SN32 = 0,
	.min.value.SN32 = 0,
	.max.value.SN32 = 1,
};

static void on_output_battery_changed(struct VeItem *droot, struct VeItem *setting,
					      const void *data)
{
	VE_UNUSED(setting);
	VE_UNUSED(data);

	ble_dbus_mark_delete_pending(droot);
}

static const struct dev_setting smart_orion_settings[] = {
	{
		.name		= "Settings/OutputBattery",
		.props		= &output_battery_props,
		.onchange	= on_output_battery_changed,
	},
};

static const char *smart_orion_get_role(struct VeItem *root)
{
	return veItemValueInt(root, "Settings/OutputBattery") ? "dcdc" : "alternator";
}

static const struct reg_info smart_orion_adv[] = {
	{
		// Device state - VE_REG_DEVICE_STATE
		.type	= VE_UN8,
		.offset	= 0 / 8,
		.shift	= 0 % 8,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "State",
		.formatter = formatters_state,
	},
	{
		// Charger error - VE_REG_CHR_ERROR_CODE
		.type	= VE_UN8,
		.offset	= 8 / 8,
		.shift	= 8 % 8,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ErrorCode",
		.formatter = formatters_charger_error,
	},
	{
		// Input voltage - VE_REG_DC_INPUT_VOLTAGE
		.type	= VE_UN16,
		.offset	= 16 / 8,
		.shift	= 16 % 8,
		.scale	= 100,
		.inval	= 0xffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/In/V",
		.format	= &veUnitVolt2Dec,
	},
	{
		// Output voltage - VE_REG_DC_CHANNEL1_VOLTAGE
		.type	= VE_SN16,
		.offset	= 32 / 8,
		.shift	= 32 % 8,
		.scale	= 100,
		.inval	= 0x7fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Dc/0/Voltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		// Off reason - VE_REG_DEVICE_OFF_REASON_2
		.type	= VE_UN32,
		.offset	= 48 / 8,
		.shift	= 48 % 8,
		.name	= "DeviceOffReason",
		.format	= &veUnitNone,
	},
};

static const struct dev_info smart_orion_dev_info = {
	.get_role     = smart_orion_get_role,
	.unknown_name = "Unknown Smart Orion",
	.num_settings = array_size(smart_orion_settings),
	.settings     = smart_orion_settings,
	.num_regs     = array_size(smart_orion_adv),
	.regs	      = smart_orion_adv,
};

const struct victron_device smart_orion_victron_device = {
	.dev_info = &smart_orion_dev_info,
	.def_name = "Smart Orion",
};
