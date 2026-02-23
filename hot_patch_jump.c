#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include "nrf.h"
#include "bsp.h"
#include "SEGGER_RTT.h"

// ================= RTT minimal output =================
static void rtt_putc(char c) { SEGGER_RTT_PutChar(0, c); }
static void rtt_puts(const char *s) { SEGGER_RTT_WriteString(0, s); }
static volatile uintptr_t g_patch_resume = 0;

static void rtt_put_u32(uint32_t v) {
  char buf[11];
  int i = 10;
  buf[i--] = '\0';
  if (v == 0) { buf[i] = '0'; rtt_puts(&buf[i]); return; }
  while (v && i >= 0) { buf[i--] = (char)('0' + (v % 10)); v /= 10; }
  rtt_puts(&buf[i + 1]);
}

static void rtt_put_hex32(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  rtt_puts("0x");
  for (int i = 7; i >= 0; --i) rtt_putc(hex[(v >> (i * 4)) & 0xF]);
}
int putchar(int c) { rtt_putc((char)c); return c; }
int puts(const char *s) { rtt_puts(s); rtt_puts("\r\n"); return 0; }
int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') { rtt_putc(*p); continue; }
    p++;
    if (*p == '%') { rtt_putc('%'); continue; }
    if (*p == 'c') { char ch = (char)va_arg(ap, int); rtt_putc(ch); continue; }
    if (*p == 's') { const char *s = va_arg(ap, const char*); if (s) rtt_puts(s); continue; }
    if (*p == 'u') { uint32_t v = va_arg(ap, uint32_t); rtt_put_u32(v); continue; }
    rtt_putc('%'); rtt_putc(*p);
  }
  va_end(ap);
  return 0;
}

//下面是本次实验正式主要代码
void fun1(void){
  rtt_puts("this is fun1\n");
}

__attribute__((naked, noinline, used, section(".text.hotpatch"), aligned(2)))
void patch_slot(void) {
    __asm volatile(
        ".thumb                \n"
        ".hword 0xE7FF         \n"
        "nop                   \n"
        "push {lr}             \n"
        "bl   fun1             \n"
        "pop  {pc}             \n"
    );
}

__attribute__((noinline))void fun2(void){
  g_patch_resume = (((uintptr_t)patch_slot) & ~(uintptr_t)1u) + 2;
  rtt_puts("this is fun2\n");
  void (*resume)(void) = (void (*)(void))(g_patch_resume | 1u);
  resume();
}
static bool encode_thumb(uintptr_t from_halfword_addr, uintptr_t to_func_addr, uint16_t *out_hw)
{
    uintptr_t from = from_halfword_addr & ~(uintptr_t)1u;
    uintptr_t to   = to_func_addr & ~(uintptr_t)1u;
    int32_t diff = (int32_t)to - (int32_t)(from + 4);
    uint16_t imm11 = (uint16_t)(((uint32_t)(diff >> 1)) & 0x07FFu);
    *out_hw = (uint16_t)(0xE000u | imm11);
    return true;
}
int main(void) {
  bsp_board_init(BSP_INIT_LEDS);

  SEGGER_RTT_Init();
  SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

  rtt_puts("test begin\n");
  patch_slot();
  //开始计算跳转的机器码
  uint16_t b_instr = 0;
  uintptr_t patch_addr = (uintptr_t)patch_slot & ~1u;
  encode_thumb(patch_addr, (uintptr_t)fun2, &b_instr);
  //开始写操作，打补丁
  uint32_t aligned_addr = patch_addr & ~3u;
  uint32_t old_val = *(volatile uint32_t *)aligned_addr;
  uint32_t new_val;
  if ((patch_addr & 2) == 0) {
      new_val = (old_val & 0xFFFF0000) | b_instr;
  } else {
      new_val = (old_val & 0x0000FFFF) | ((uint32_t)b_instr << 16);
  } 
  NRF_NVMC->CONFIG = 1; 
  while (NRF_NVMC->READY == 0);
  *(volatile uint32_t *)aligned_addr = new_val;
  while (NRF_NVMC->READY == 0);
  NRF_NVMC->CONFIG = 0; 
  while (NRF_NVMC->READY == 0);
  __DSB();
  __ISB();
  //测试补丁
  patch_slot();
  //开始写操作，卸载补丁
  old_val = *(volatile uint32_t *)aligned_addr; 
  uint16_t unpatch_instr = 0xE000;
  if ((patch_addr & 2) == 0) {
      new_val = (old_val & 0xFFFF0000) | unpatch_instr;
  } else {
      new_val = (old_val & 0x0000FFFF) | ((uint32_t)unpatch_instr << 16);
  }
  NRF_NVMC->CONFIG = 1;
  while (NRF_NVMC->READY == 0);
  *(volatile uint32_t *)aligned_addr = new_val;
  while (NRF_NVMC->READY == 0);
  NRF_NVMC->CONFIG = 0;
  while (NRF_NVMC->READY == 0);
  __DSB();
  __ISB(); 
  //测试补丁是否卸载
  patch_slot();
  rtt_puts("test end\n");
}
