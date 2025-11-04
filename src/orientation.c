#include <math.h>

#include "ble-dbus.h"

#define deg2rad(x) ((x) * M_PI / 180)
#define rad2deg(x) ((x) * 180 / M_PI)

static float length2(float x, float y)
{
	return sqrtf(x * x + y * y);
}

static float length3(float x, float y, float z)
{
	return sqrtf(x * x + y * y + z * z);
}

static void rpy2rot(float ph, float th, float ps, float r[3][3])
{
	float cph = cosf(ph);
	float sph = sinf(ph);
	float cth = cosf(th);
	float sth = sinf(th);
	float cps = cosf(ps);
	float sps = sinf(ps);

	r[0][0] = cps * cth + sth * sph * sps;
	r[0][1] = -cth * sps + cps * sph * sth;
	r[0][2] = cph * sth;

	r[1][0] = cph * sps;
	r[1][1] = cph * cps;
	r[1][2] = -sph;

	r[2][0] = cth * sph * sps - sth * cps;
	r[2][1] = cps * cth * sph + sth * sps;
	r[2][2] = cth * cph;
}

static void rotate(float r[3][3], float *px, float *py, float *pz)
{
	float x = *px;
	float y = *py;
	float z = *pz;

	*px = r[0][0] * x + r[0][1] * y + r[0][2] * z;
	*py = r[1][0] * x + r[1][1] * y + r[1][2] * z;
	*pz = r[2][0] * x + r[2][1] * y + r[2][2] * z;
}

static int orientation_calc(struct VeItem *root, float *ph, float *th, int ref)
{
	float ax = veItemValueFloat(root, "AccelX");
	float ay = veItemValueFloat(root, "AccelY");
	float az = veItemValueFloat(root, "AccelZ");
	float ar = length3(ax, ay, az);

	/* Non-gravitational forces on sensor */
	if (ar < 0.9 || ar > 1.1)
		return -1;

	float rph, rth, rps;
	float r[3][3];

	if (ref) {
		rph = veItemValueFloat(root, "Orientation/Ref/Roll");
		rth = veItemValueFloat(root, "Orientation/Ref/Pitch");
	} else {
		rph = 0;
		rth = 0;
	}

	rps = veItemValueFloat(root, "Orientation/Ref/Yaw");

	rpy2rot(deg2rad(rph), deg2rad(rth), deg2rad(rps), r);
	rotate(r, &ax, &ay, &az);

	*ph = atan2f(ay, copysignf(length2(az, 0.1 * ax), az));
	*th = atan2f(-ax, length2(ay, az));

	return 0;
}

static struct VeSettingProperties rot_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= -180,
	.max.value.Float	= 180,
};

static const struct dev_setting orientation_settings[] = {
	{
		.name	= "Orientation/Ref/Roll",
		.props	= &rot_props,
	},
	{
		.name	= "Orientation/Ref/Pitch",
		.props	= &rot_props,
	},
	{
		.name	= "Orientation/Ref/Yaw",
		.props	= &rot_props,
	},
};

static void setref_changed(struct VeItem *item)
{
	struct VeItem *root = veItemCtx(item)->ptr;
	VeVariant val;
	float ph, th;
	int err;

	if (!veItemIsValid(item))
		goto end;

	veItemLocalValue(item, &val);
	veVariantToN32(&val);

	if (!val.value.SN32)
		goto end;

	err = orientation_calc(root, &ph, &th, 0);
	if (err)
		return;

	ble_dbus_set_float(root, "Orientation/Ref/Roll", rad2deg(ph));
	ble_dbus_set_float(root, "Orientation/Ref/Pitch", rad2deg(th));

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
	float ph, th;
	int err;

	err = orientation_calc(root, &ph, &th, 1);
	if (err)
		return;

	ble_dbus_set_float(root, "Orientation/Roll", rad2deg(ph));
	ble_dbus_set_float(root, "Orientation/Pitch", rad2deg(th));
}

