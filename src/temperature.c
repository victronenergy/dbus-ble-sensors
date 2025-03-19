#include "ble-dbus.h"

static struct VeSettingProperties temp_type_props = {
	.type		= VE_SN32,
	.def.value.SN32	= 2,
	.min.value.SN32	= 0,
	.max.value.SN32	= 6,
};

static const struct dev_setting temp_settings[] = {
	{
		.name	= "TemperatureType",
		.props	= &temp_type_props,
	},
};

const struct dev_class temperature_class = {
	.role		= "temperature",
	.num_settings	= array_size(temp_settings),
	.settings	= temp_settings,
};
