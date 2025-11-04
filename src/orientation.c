#include <math.h>

#include "ble-dbus.h"
#include "orientation.h"

#define deg2rad(x) ((x) * M_PI / 180)
#define rad2deg(x) ((x) * 180 / M_PI)
#define AR_MIN 0.95f
#define AR_MAX 1.05f
#define EPS 1e-6f

static struct VeSettingProperties fwd_off_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= -180,
	.max.value.Float	= 180,
};
static struct VeSettingProperties refup_x_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= -1,
	.max.value.Float	= 1,
};
static struct VeSettingProperties refup_y_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 0,
	.min.value.Float	= -1,
	.max.value.Float	= 1,
};
static struct VeSettingProperties refup_z_props = {
	.type			= VE_FLOAT,
	.def.value.Float	= 1,
	.min.value.Float	= -1,
	.max.value.Float	= 1,
};

static float normalize_vec(float vec[3])
{
	float len = sqrtf(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
	if (len < EPS)
		return 0.0f;
	vec[0] /= len; vec[1] /= len; vec[2] /= len;
	return len;
}

static void cross_product(float a[3], float b[3], float result[3])
{
	result[0] = a[1]*b[2] - a[2]*b[1];
	result[1] = a[2]*b[0] - a[0]*b[2];
	result[2] = a[0]*b[1] - a[1]*b[0];
}

/* Rotates x and y vectors around the origin by the given angle. */
static void rotate_xy(float x[3], float y[3], float angle)
{
	float cos_a = cosf(angle);
	float sin_a = sinf(angle);
	float x_new[3] = {
		cos_a * x[0] + sin_a * y[0],
		cos_a * x[1] + sin_a * y[1],
		cos_a * x[2] + sin_a * y[2]
	};
	float y_new[3] = {
		-sin_a * x[0] + cos_a * y[0],
		-sin_a * x[1] + cos_a * y[1],
		-sin_a * x[2] + cos_a * y[2]
	};
	x[0] = x_new[0]; x[1] = x_new[1]; x[2] = x_new[2];
	y[0] = y_new[0]; y[1] = y_new[1]; y[2] = y_new[2];
}

/* Rotates vec to the new reference frame defined by the x, y, z axes */
static void rotate_3d(float vec[3], float x[3], float y[3], float z[3])
{
	/* The rotation matrix is:
	 * | x[0] x[1] x[2] |
	 * | y[0] y[1] y[2] |
	 * | z[0] z[1] z[2] |
	 */
	float x_new = x[0]*vec[0] + x[1]*vec[1] + x[2]*vec[2];
	float y_new = y[0]*vec[0] + y[1]*vec[1] + y[2]*vec[2];
	float z_new = z[0]*vec[0] + z[1]*vec[1] + z[2]*vec[2];
	vec[0] = x_new; vec[1] = y_new; vec[2] = z_new;
}

static void calibration_changed(struct VeItem *root, struct VeItem *item, const void *data)
{
	VE_UNUSED(item);
	VE_UNUSED(data);
	orientation_update(root);
}

static const struct dev_setting orientation_settings[] = {
    { .name = "Orientation/ForwardOffset", .props = &fwd_off_props, .onchange = calibration_changed },
    { .name = "Orientation/RefUpX", .props = &refup_x_props, .onchange = calibration_changed },
    { .name = "Orientation/RefUpY", .props = &refup_y_props, .onchange = calibration_changed },
    { .name = "Orientation/RefUpZ", .props = &refup_z_props, .onchange = calibration_changed },
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

	float a[3] = {
		veItemValueFloat(root, "AccelX"),
		veItemValueFloat(root, "AccelY"),
		veItemValueFloat(root, "AccelZ")
	};
	float ar = normalize_vec(a);
	if (ar < AR_MIN || ar > AR_MAX)
		goto end;

	/* Store up vector. Use set_remote so the values are updated in localsettings */
	ble_dbus_set_remote_float(root, "Orientation/RefUpX", a[0]);
	ble_dbus_set_remote_float(root, "Orientation/RefUpY", a[1]);
	ble_dbus_set_remote_float(root, "Orientation/RefUpZ", a[2]);

	ble_dbus_set_float(root, "Orientation/Pitch", 0.0f);
	ble_dbus_set_float(root, "Orientation/Roll", 0.0f);
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

	ble_dbus_create_item(root, "Orientation/Pitch", veVariantInvalidType(&val, VE_FLOAT), &veUnitNone);
	ble_dbus_create_item(root, "Orientation/Roll", veVariantInvalidType(&val, VE_FLOAT), &veUnitNone);
}

void orientation_update(struct VeItem *root)
{
	/* Check the input acceleration vector, whether it's decent */
	float a[3] = {
		veItemValueFloat(root, "AccelX"),
		veItemValueFloat(root, "AccelY"),
		veItemValueFloat(root, "AccelZ")
	};
	float ar = normalize_vec(a);
	if (ar < AR_MIN || ar > AR_MAX)
		goto end;

	/* Load up vector from settings and check whether it's decent */
	float up[3] = {
		veItemValueFloat(root, "Orientation/RefUpX"),
		veItemValueFloat(root, "Orientation/RefUpY"),
		veItemValueFloat(root, "Orientation/RefUpZ")
	};
	float up_mag = normalize_vec(up);
	if (up_mag < AR_MIN || up_mag > AR_MAX)
		goto end;

	/* We want to calculate pitch and roll in a reference frame that is
	 * aligned with the vehicle, meaning the Z points straight up, X points
	 * in the forward direction, Y points to the side and the XY plane is
	 * parallel to the ground (and thus perpendicular to the Z axis). To do
	 * this, we need to construct a rotation matrix to rotate the input
	 * acceleration vector to that new reference frame.
	 *
	 * In this reference frame, the Z axis is aligned with the RefUp vector.
	 * We choose the direction of the new X axis based on the orientation of
	 * the RefUp vector. When the device is mounted flat, RefUp has a large Z
	 * component. In that case, we use the original X axis direction as
	 * forward reference. When the Z components is small, it means the device
	 * is mounted on its side. In that case, we use the original Z axis as
	 * forward reference.
	 *
	 * This is because when the device is flat, we know for sure that the X
	 * axis is more or less parallel to the ground and we can use that
	 * direction as forward.
	 * When the device is mounted upright, the Z axis is more or less
	 * parallel to the ground and the RefUp vector can consist of both X
	 * and Y components due to the mounting rotation of the device.
	 *
	 * Now that we have a Z/up and X/forward vector, we can construct a
	 * Y/left vector using the cross product. As mentioned, the XY plane is
	 * parallel to the ground, so we can now apply the ForwardOffset as a
	 * simple rotation to forward and left. This makes forward point in the
	 * actual forward direction of the vehicle.
	 *
	 * Now we have a new reference frame defined by the forward, left and
	 * up vector. These vectors form a rotation matrix that can be used to
	 * rotate the input acceleration vector to this new reference frame.
	 * The pitch and roll are then calculated from this transformed input vector.
	 */

	 /* Determine the reference forward vector based on the orientation of the up vector */
	float ref_frwd[3];
	if (fabsf(up[2]) > 0.7f) {
 		/* FLAT: Use X axis as forward reference */
		ref_frwd[0] = 1.0f; ref_frwd[1] = 0.0f; ref_frwd[2] = 0.0f;
 	} else {
 		/* SIDE: Use Z axis as forward reference */
		ref_frwd[0] = 0.0f; ref_frwd[1] = 0.0f; ref_frwd[2] = 1.0f;
 	}

	/* calculate the forward vector in the same direction as ref_frwd, but perpendicular to up */
	float dot = ref_frwd[0]*up[0] + ref_frwd[1]*up[1] + ref_frwd[2]*up[2];
	float frwd[3] = {
		ref_frwd[0] - dot*up[0],
		ref_frwd[1] - dot*up[1],
		ref_frwd[2] - dot*up[2]
	};
	normalize_vec(frwd);

	/* Left vector = up x forward */
	float left[3];
	cross_product(up, frwd, left);
	normalize_vec(left);

	/* Apply ForwardOffset to rotate forward and left. */
	float angle = deg2rad(veItemValueFloat(root, "Orientation/ForwardOffset"));
	rotate_xy(frwd, left, angle);

	/* transform the acceleration vector to the new reference frame */
	rotate_3d(a, frwd, left, up);

	/* Now calculate pitch and roll according eqn 25 and 26 in
	 * https://www.nxp.com/docs/en/application-note/AN3461.pdf
	 */
	float pitch = atan2f(-a[0], sqrtf(a[1]*a[1] + a[2]*a[2]));
	float roll = atan2f(a[1], a[2]);

	ble_dbus_set_float(root, "Orientation/Pitch", rad2deg(pitch));
	ble_dbus_set_float(root, "Orientation/Roll", rad2deg(roll));
	return;
end:
	ble_dbus_set_invalid(root, "Orientation/Pitch");
	ble_dbus_set_invalid(root, "Orientation/Roll");
}
