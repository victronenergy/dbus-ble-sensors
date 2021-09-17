#ifndef TASK_H
#define TASK_H

#include <velib/types/ve_item.h>

#define TICKS_PER_SEC	20

struct VeItem *get_settings(void);
struct VeItem *get_control(void);

#endif
