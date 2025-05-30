#ifndef TANK_H
#define TANK_H

#define FLUID_TYPE_FRESH_WATER		1
#define FLUID_TYPE_WASTE_WATER		2
#define FLUID_TYPE_LIVE_WELL		3
#define FLUID_TYPE_OIL			4
#define FLUID_TYPE_BLACK_WATER		5
#define FLUID_TYPE_GASOLINE		6
#define FLUID_TYPE_DIESEL		7
#define FLUID_TYPE_LPG			8
#define FLUID_TYPE_LNG			9
#define FLUID_TYPE_HYDRAULIC_OIL	10
#define FLUID_TYPE_RAW_WATER		11

#define TANK_SHAPE_LINEAR		0
#define TANK_SHAPE_CUSTOM		1
#define MAX_SHAPE_POINTS		10

struct tank_info {
	uint32_t	flags;
};

struct tank_shape_point {
	float height;
	float volume;
};

struct tank_shape {
	int type;
	int num_points;
	struct tank_shape_point points[MAX_SHAPE_POINTS];
};

#define TANK_FLAG_TOPDOWN	(1 << 0)

extern const struct dev_class tank_class;

float tank_shape_calculate_volume(const struct tank_shape *shape, float height);
float tank_shape_calculate_remaining(struct VeItem *root, float raw_height);
int tank_shape_parse_from_string(const char *shape_str, struct tank_shape *shape);

#endif
