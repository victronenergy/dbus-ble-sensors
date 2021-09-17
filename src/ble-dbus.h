#ifndef BLE_DBUS_H
#define BLE_DBUS_H

#include <stdint.h>

#include <velib/types/types.h>
#include <velib/types/variant.h>
#include <velib/types/ve_item.h>

#define array_size(a) (sizeof(a) / sizeof(a[0]))

struct dev_setting {
	char		*name;
	struct VeSettingProperties *props;
};

struct dev_info {
	uint16_t	product_id;
	uint16_t	dev_instance;
	const char	*dev_class;
	const char	*dev_prefix;
	const char	*role;
	int		num_settings;
	const struct dev_setting *settings;
};

struct reg_info {
	uint16_t	type;
	uint16_t	offset;
	uint16_t	shift;
	uint32_t	mask;
	float		scale;
	float		bias;
	uint32_t	inval;
	uint32_t	flags;
	const char	*name;
	const void	*format;
};

#define REG_FLAG_BIG_ENDIAN	(1 << 0)
#define REG_FLAG_INVALID	(1 << 1)

int ble_dbus_init(void);
int ble_dbus_set_regs(const char *dev, const struct dev_info *info,
                      const struct reg_info *regs, int nregs,
                      const uint8_t *data, int len);
int ble_dbus_set_name(const char *dev, const char *name);
void ble_dbus_tick(void);

#endif
