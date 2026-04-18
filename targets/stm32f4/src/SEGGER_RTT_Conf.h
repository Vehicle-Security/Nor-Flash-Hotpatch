/*
 * SEGGER_RTT_Conf.h — Minimal RTT configuration for STM32 targets.
 *
 * Replaces the Nordic SDK version which depends on nordic_common.h.
 * This is a standalone configuration that works on any ARM Cortex-M.
 */
#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#ifdef __IAR_SYSTEMS_ICC__
  #include <intrinsics.h>
#endif

/*********************************************************************
*       Defines, configurable
*/
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS       3
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS     3

#define BUFFER_SIZE_UP                      1024
#define BUFFER_SIZE_DOWN                    64

#define SEGGER_RTT_PRINTF_BUFFER_SIZE       256u

#define USE_RTT_ASM                         0

#define SEGGER_RTT_MODE_DEFAULT             0   /* SEGGER_RTT_MODE_NO_BLOCK_SKIP */

/*********************************************************************
*       Lock/unlock for thread safety (bare-metal: disable interrupts)
*/
#if (defined __GNUC__)
  #define SEGGER_RTT_LOCK()   { unsigned int _primask; __asm volatile("mrs %0, primask\ncpsid i" : "=r"(_primask) :: "memory");
  #define SEGGER_RTT_UNLOCK() __asm volatile("msr primask, %0" :: "r"(_primask) : "memory"); }
#elif (defined __IAR_SYSTEMS_ICC__)
  #define SEGGER_RTT_LOCK()   { unsigned int _primask = __get_PRIMASK(); __disable_irq();
  #define SEGGER_RTT_UNLOCK() __set_PRIMASK(_primask); }
#else
  #define SEGGER_RTT_LOCK()
  #define SEGGER_RTT_UNLOCK()
#endif

#define SEGGER_RTT_MEMCPY_USE_BYTELOOP     0

#endif
