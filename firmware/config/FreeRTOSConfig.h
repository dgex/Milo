#ifndef COMPANION_FREERTOS_CONFIG_H
#define COMPANION_FREERTOS_CONFIG_H
#include "sdk_freertos_base.h"
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE ((size_t)(160 * 1024))
#undef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 2
#undef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK 1
#undef INCLUDE_uxTaskGetStackHighWaterMark
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#endif

