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

struct alarm {
	const char	*name;
	const char	*item;
	int		dir;
	float		level;
	float		hyst;
	float		(*get_level)(struct VeItem *root,
				     const struct alarm *alarm);
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
	int		(*xlate)(struct VeItem *root, VeVariant *val,
				 uint64_t rawval);
	const char	*name;
	const void	*format;
};

#define REG_FLAG_BIG_ENDIAN	(1 << 0)
#define REG_FLAG_INVALID	(1 << 1)

struct dev_info {
	uint16_t	product_id;
	uint16_t	dev_instance;
	const char	*dev_prefix;
	const char	*role;
	int		num_settings;
	const struct dev_setting *settings;
	int		num_regs;
	const struct reg_info *regs;
	int		num_alarms;
	const struct alarm *alarms;
	int		(*init)(struct VeItem *root, const void *data);
};

#define STATUS_OK		0
#define STATUS_BATT_LOW		5

extern const VeVariantUnitFmt veUnitHectoPascal;
extern const VeVariantUnitFmt veUnitG2Dec;
extern const VeVariantUnitFmt veUnitdBm;
extern const VeVariantUnitFmt veUnitcm;
extern const VeVariantUnitFmt veUnitm3;

int ble_dbus_init(void);
int ble_dbus_add_interface(const char *name, const char *addr);
struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info,
			       const void *data);
struct VeItem *ble_dbus_get_dev(const char *dev);
int ble_dbus_add_settings(struct VeItem *droot,
			  const struct dev_setting *settings,
			  int num_settings);
int ble_dbus_is_enabled(struct VeItem *root);
int ble_dbus_set_regs(struct VeItem *root, const uint8_t *data, int len);
int ble_dbus_set_name(struct VeItem *root, const char *name);
int ble_dbus_set_item(struct VeItem *root, const char *path, VeVariant *val,
		      const void *format);
int ble_dbus_set_str(struct VeItem *root, const char *path, const char *str);
int ble_dbus_set_int(struct VeItem *root, const char *path, int num);
void ble_dbus_update_alarms(struct VeItem *droot);
int ble_dbus_update(struct VeItem *root);
void ble_dbus_tick(void);

#endif
