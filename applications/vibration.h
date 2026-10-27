#ifndef __VIBRATION_H
#define __VIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void Vibration_Init(void);
uint32_t Vibration_GetCount(void);
void Vibration_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __VIBRATION_H */
