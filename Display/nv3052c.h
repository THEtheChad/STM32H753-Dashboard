#ifndef NV3052C_H
#define NV3052C_H

#include "main.h"
#include <stdint.h>

#define NV3052C_WIDTH  720
#define NV3052C_HEIGHT 720

void     NV3052C_Init(void);            /* panel init + render static gauge face */
void     NV3052C_Update(void);          /* no-op until real-time data arrives */

#endif /* NV3052C_H */
