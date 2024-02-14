#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32F103xB)
#include <stm32f1xx_ll_utils.h>
#elif defined (STM32F303xC)
#include <stm32f3xx_ll_utils.h>
#endif

#define utilmDelay(ms) LL_mDelay(ms)

#ifdef __cplusplus
}
#endif

#endif // UTILS_H
