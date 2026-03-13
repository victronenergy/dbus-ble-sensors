#ifndef BLE_DBUS_H
#define BLE_DBUS_H

#include <stddef.h>
#include <stdint.h>

#include <velib/types/types.h>
#include <velib/types/variant.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>

#include "ble-handler.h"

#define align(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define alloc_size(x) align(x, sizeof(max_align_t))
#define array_size(a) (sizeof(a) / sizeof(a[0]))

typedef void (*setting_changed_fn)(struct VeItem *root, struct VeItem *setting,
				   const void *data);

struct dev_setting {
	char		*name;
	struct VeSettingProperties *props;
	setting_changed_fn onchange;
};

struct alarm {
	const char	*name;
	const char	*item;
	uint32_t	flags;
	float		level;
	float		hyst;
	float		(*get_level)(struct VeItem *root,
				     const struct alarm *alarm);
	struct VeSettingProperties *active;
	struct VeSettingProperties *restore;
};

#define ALARM_FLAG_HIGH		(1 << 0)
#define ALARM_FLAG_CONFIG	(1 << 1)

struct reg_info {
	uint16_t	type;
	uint16_t	offset;
	uint16_t	shift;
	uint16_t	bits;
	float		scale;
	float		bias;
	uint32_t	inval;
	uint32_t	flags;
	uint32_t	key;
	int		(*xlate)(struct VeItem *root, VeVariant *val,
				 uint64_t rawval);
	const char	*name;
	const void	*format;
};

#define REG_FLAG_BIG_ENDIAN	(1 << 0)
#define REG_FLAG_INVALID	(1 << 1)
#define REG_FLAG_KEY		(1 << 2)
#define REG_FLAG_WARN_ALARM	(1 << 3)

struct dev_class {
	const char	*role;
	int		num_settings;
	const struct dev_setting *settings;
	int		num_alarms;
	const struct alarm *alarms;
	int		pdata_size;
	void		(*init)(struct VeItem *root, const void *data);
	void		(*update)(struct VeItem *root, const void *data);
};

struct dev_info {
	const struct dev_class *dev_class;
	uint16_t	product_id;
	const char 	*unknown_name;
	veBool		use_ble_name;
	uint16_t	dev_instance;
	const char	*dev_prefix;
	const char	*role;
	int		num_settings;
	const struct dev_setting *settings;
	uint32_t	reg_key;
	int		num_regs;
	const struct reg_info *regs;
	int		num_alarms;
	const struct alarm *alarms;
	int		pdata_size;
	int		(*init)(struct VeItem *root, const void *data);
	int		seqnr_bits;
	uint32_t	seqnr_window;
};

#define STATUS_OK		0
#define STATUS_BATT_LOW		5

enum name_source {
	NAME_ORIG_BLE,
	NAME_ORIG_DEVICE,
	NAME_ORIG_CUSTOM,

	NAME_ORIG_NONE,
};

extern const VeVariantUnitFmt veUnitHectoPascal;
extern const VeVariantUnitFmt veUnitG2Dec;
extern const VeVariantUnitFmt veUnitdBm;
extern const VeVariantUnitFmt veUnitcm;
extern const VeVariantUnitFmt veUnitm3;
extern const VeVariantUnitFmt veUnitPPM;
extern const VeVariantUnitFmt veUnitUgM3;
extern const VeVariantUnitFmt veUnitLux;
extern const VeVariantUnitFmt veUnitIndex;

int ble_dbus_init(void);
int ble_dbus_add_interface(const char *name, const char *addr);
struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info,
			       const void *data);
struct VeItem *ble_dbus_get_dev(const char *dev);
void *ble_dbus_get_pdata(struct VeItem *root);
void *ble_dbus_get_cdata(struct VeItem *root);
int ble_dbus_add_settings(struct VeItem *droot,
			  const struct dev_setting *settings,
			  int num_settings);
int ble_dbus_add_control_settings(struct VeItem *droot, const struct dev_setting *settings,
				  int num_settings);
int ble_dbus_add_alarms(struct VeItem *droot, const struct alarm *alarms,
			int num_alarms);
int ble_dbus_is_enabled(struct VeItem *root);
int ble_dbus_set_regs(struct VeItem *root, const uint8_t *data, int len);
int ble_dbus_set_name(struct VeItem *root, const char *name, enum name_source source);
struct VeItem *ble_dbus_create_item(struct VeItem *droot, const char *path, VeVariant *val,
				    const void *format);
struct VeItem *ble_dbus_create_str(struct VeItem *root, const char *path, const char *str);
struct VeItem *ble_dbus_create_int(struct VeItem *root, const char *path, int num);
int ble_dbus_set_item(struct VeItem *root, const char *path, VeVariant *val);
int ble_dbus_set_str(struct VeItem *root, const char *path, const char *str);
int ble_dbus_set_int(struct VeItem *root, const char *path, int num);
int ble_dbus_set_float(struct VeItem *root, const char *path, float num);
int ble_dbus_set_invalid(struct VeItem *root, const char *path);
struct VeItem *ble_dbus_get_control_item(struct VeItem *droot, const char *name);
void ble_dbus_update_alarms(struct VeItem *droot);
int ble_dbus_update(struct VeItem *root);
void ble_dbus_tick(void);

veBool ble_dbus_check_dup(struct VeItem *root, enum data_source source);
veBool ble_dbus_check_dup_seq(struct VeItem *root, enum data_source source, uint32_t seqnr);

#endif
