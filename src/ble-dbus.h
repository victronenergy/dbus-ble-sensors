#ifndef BLE_DBUS_H
#define BLE_DBUS_H

#include <stdint.h>

#include <velib/types/types.h>
#include <velib/types/variant.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>

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

#define STATUS_OK		0
#define STATUS_BATT_LOW		5

extern const VeVariantUnitFmt veUnitHectoPascal;
extern const VeVariantUnitFmt veUnitG2Dec;
extern const VeVariantUnitFmt veUnitdBm;

int ble_dbus_init(void);
int ble_dbus_add_interface(const char *name, const char *addr);
struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info);
struct VeItem *ble_dbus_get_dev(const char *dev);
int ble_dbus_is_enabled(struct VeItem *root);
int ble_dbus_set_regs(struct VeItem *root,
                      const struct reg_info *regs, int nregs,
                      const uint8_t *data, int len);
int ble_dbus_set_name(struct VeItem *root, const char *name);
int ble_dbus_update(struct VeItem *root);
void ble_dbus_tick(void);

#endif
