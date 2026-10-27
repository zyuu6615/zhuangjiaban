#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void OLED_Init(void);
void OLED_ShowCount(uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */
