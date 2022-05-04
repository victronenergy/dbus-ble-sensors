#ifndef MOPEKA_H
#define MOPEKA_H

#include <stdint.h>

#define MFG_ID_MOPEKA 0x0059

int mopeka_handle_mfg(const uint8_t *buf, int len);

#endif
