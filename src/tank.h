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

struct tank_info {
	uint32_t	flags;
};

#define TANK_FLAG_TOPDOWN	(1 << 0)

extern const struct dev_class tank_class;

#endif
