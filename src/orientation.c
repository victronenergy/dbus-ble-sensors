#include <math.h>

#include "ble-dbus.h"

#define deg2rad(x) ((x) * M_PI / 180)
#define rad2deg(x) ((x) * 180 / M_PI)

static float length(float x, float y, float z)
{
	return sqrtf(x * x + y * y + z * z);
}

static float rect2sphere(float x, float y, float z, float *th, float *ph)
{
	float r = length(x, y, z);

	*th = acosf(z / r);
	*ph = atan2f(y, x);

	return r;
}

static struct VeSettingProperties ref_tilt_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= 0,
	.max.value.Float	= 180,
};

static struct VeSettingProperties ref_dir_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= -180,
	.max.value.Float	= 180,
};

static const struct dev_setting orientation_settings[] = {
	{
		.name	= "Orientation/RefTilt",
		.props	= &ref_tilt_props,
	},
	{
		.name	= "Orientation/RefDir",
		.props	= &ref_dir_props,
	},
};

static void setref_changed(struct VeItem *item)
{
	struct VeItem *root = veItemCtx(item)->ptr;
	VeVariant val;

	if (!veItemIsValid(item))
		goto end;

	veItemLocalValue(item, &val);
	veVariantToN32(&val);

	if (!val.value.SN32)
		goto end;

	float ax = veItemValueFloat(root, "AccelX");
	float ay = veItemValueFloat(root, "AccelY");
	float az = veItemValueFloat(root, "AccelZ");
	float r, th, ph;

	r = rect2sphere(ax, ay, az, &th, &ph);

	if (r > 0.95 && r < 1.05) {
		ble_dbus_set_float(root, "Orientation/RefTilt", rad2deg(th));
		ble_dbus_set_float(root, "Orientation/RefDir", rad2deg(ph));
	}

end:
	veItemInvalidate(item);
}

void orientation_init(struct VeItem *root)
{
	struct VeItem *setref;
	VeVariant val;

	ble_dbus_add_settings(root, orientation_settings,
			      array_size(orientation_settings));

	setref = veItemGetOrCreateUid(root, "Orientation/SetRef");
	veItemCtx(setref)->ptr = root;
	veItemOwnerSet(setref, veVariantInvalidType(&val, VE_SN32));
	veItemSetChanged(setref, setref_changed);
}

void orientation_update(struct VeItem *root)
{
	float ax = veItemValueFloat(root, "AccelX");
	float ay = veItemValueFloat(root, "AccelY");
	float az = veItemValueFloat(root, "AccelZ");
	float ar = length(ax, ay, az);

	/* Non-gravitational forces on sensor */
	if (ar < 0.95 || ar > 1.05)
		return;

	float rth = veItemValueFloat(root, "Orientation/RefTilt");
	float rph = veItemValueFloat(root, "Orientation/RefDir");

	rth = deg2rad(rth);
	rph = deg2rad(rph);

	float sth = sinf(rth), cth = cosf(rth);
	float sph = sinf(rph), cph = cosf(rph);

	float dr  = ax * sth * cph + ay * sth * sph + az * cth;
	float dth = ax * cth * cph + ay * cth * sph - az * sth;
	float dph = -ax * sph + ay * cph;

	float oth, oph;
	rect2sphere(dth, dph, dr, &oth, &oph);

	ble_dbus_set_float(root, "Orientation/Tilt", rad2deg(oth));
	ble_dbus_set_float(root, "Orientation/Direction", rad2deg(oph));
}

