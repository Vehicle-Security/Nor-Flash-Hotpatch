#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "nrf.h"
#include "bsp.h"
#include "SEGGER_RTT.h"

// ================= RTT minimal output =================
static void rtt_putc(char c) { SEGGER_RTT_PutChar(0, c); }
static void rtt_puts(const char *s) { SEGGER_RTT_WriteString(0, s); }

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

// 兜底：避免链接到 stdout/fileops
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

// ================= Route C: Fault-based dispatch =================
static volatile bool     g_patch_enabled = false;
static volatile uint32_t g_counter = 0;
static volatile uint32_t g_patch_hit = 0;

static volatile uint32_t g_fault_seen = 0;
static volatile uint32_t g_fault_is_udf = 0;
static volatile uint32_t g_fault_target = 0;

#define NOINLINE __attribute__((noinline))
#define USED     __attribute__((used))
#define NOCLONE  __attribute__((noclone))

NOINLINE USED NOCLONE
static int vul_body_original(void) {
  g_counter += 1;
  return (int)g_counter;
}

NOINLINE USED NOCLONE
static int vul_body_patch(void) {
  g_patch_hit ^= 1;
  g_counter += 1000;
  return (int)g_counter;
}

// ---------- 插座地址（汇编 label，在 vul_stub 里定义） ----------
extern const uint16_t __patch_slot;

// “插座 + 正常路径”：
// 初始第一条是 UDF（非法指令），会触发 Fault -> handler 分发到 patch/original。
// 如果把该半字写成 0x0000（合法），则不再 Fault，按正常路径调用 original。
NOINLINE USED NOCLONE __attribute__((naked))
static int vul_stub(void) {
  __asm volatile(
    ".syntax unified             \n"
    ".thumb                      \n"
    ".align 2                    \n"   // 4-byte 对齐，便于按 word 写 flash
    ".global __patch_slot        \n"
    "__patch_slot:               \n"
    ".hword 0xDE42               \n"   // 初始：UDF #0x42（非法插座）
    "b 1f                        \n"   // 若被写成合法指令，则走正常路径
    "1:                          \n"
    "push {lr}                   \n"
    "bl   vul_body_original      \n"
    "pop  {pc}                   \n"
  );
}

// Exception stack frame: r0,r1,r2,r3,r12,lr,pc,xpsr
static void fault_dispatch(uint32_t *stacked) {
  g_fault_seen++;

  uint32_t spc = stacked[6];
  uint32_t pc  = spc & ~1u;

  // UDF 识别：看 pc / pc-2 / pc-4
  uint16_t insn0   = *(uint16_t *)(pc);
  uint16_t insn_m2 = *(uint16_t *)(pc - 2u);
  uint16_t insn_m4 = *(uint16_t *)(pc - 4u);

  g_fault_is_udf = 0;
  if ((insn0   & 0xFF00u) == 0xDE00u) g_fault_is_udf = 1;
  if ((insn_m2 & 0xFF00u) == 0xDE00u) g_fault_is_udf = 1;
  if ((insn_m4 & 0xFF00u) == 0xDE00u) g_fault_is_udf = 1;

  // 清 fault 标志位（写 1 清除）
  SCB->CFSR = SCB->CFSR;
  SCB->HFSR = SCB->HFSR;

  if (g_fault_is_udf) {
    uint32_t target = g_patch_enabled ? (uint32_t)&vul_body_patch
                                      : (uint32_t)&vul_body_original;
    g_fault_target = target;
    stacked[6] = (target | 1u);   // 改写返回 PC：异常返回后跳到目标函数
    return;
  }

  // 兜底：避免锁死
  g_fault_target = (uint32_t)&vul_body_original;
  stacked[6] = ((uint32_t)&vul_body_original) | 1u;
}

__attribute__((naked))
static void MyHardFault(void) {
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "b fault_dispatch  \n"
  );
}

__attribute__((naked))
static void MyUsageFault(void) {
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "b fault_dispatch  \n"
  );
}

// ================= VTOR relocation =================
#define VTOR_ENTRIES (16u + 48u)
__attribute__((aligned(256)))
static uint32_t g_vtor_ram[VTOR_ENTRIES];

static void relocate_vtor_and_hook_faults(void) {
  uint32_t *vtor_flash = (uint32_t *)SCB->VTOR;
  if (vtor_flash == 0) vtor_flash = (uint32_t *)0x00000000u;

  for (uint32_t i = 0; i < VTOR_ENTRIES; i++) g_vtor_ram[i] = vtor_flash[i];

  g_vtor_ram[3] = (uint32_t)&MyHardFault;
  g_vtor_ram[6] = (uint32_t)&MyUsageFault;

  SCB->VTOR = (uint32_t)g_vtor_ram;
  __DSB(); __ISB();

  SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
  __DSB(); __ISB();
}

// ================= NVMC flash write (run from RAM) =================
static inline void nvmc_wait_ready(void) {
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

// 注意：如果启用了 SoftDevice，不要直接用 NVMC 写/擦，请改用 sd_flash_write/sd_flash_page_erase。
__attribute__((section(".fast"), noinline))
static void nvmc_write_word(uint32_t addr, uint32_t value) {
  __disable_irq();

  nvmc_wait_ready();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
  nvmc_wait_ready();

  *(volatile uint32_t *)addr = value;
  nvmc_wait_ready();

  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
  nvmc_wait_ready();

  __enable_irq();

  __DSB(); __ISB();

#ifdef NVMC_ICACHECNF_CACHEEN_Msk
  NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Disabled << NVMC_ICACHECNF_CACHEEN_Pos);
  __DSB(); __ISB();
  NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Enabled  << NVMC_ICACHECNF_CACHEEN_Pos);
  __DSB(); __ISB();
#endif
}

static uint16_t read_patch_slot_halfword(void) {
  return *(const volatile uint16_t *)&__patch_slot;
}

// 把 __patch_slot（半字）从 UDF(0xDE42) 清位到 0x0000（合法指令）
// 之后调用 vul_stub() 将不再触发 Fault（无异常开销）。
static void hard_uninstall_socket_to_nop(void) {
  uint32_t slot_addr = (uint32_t)&__patch_slot;
  uint32_t word_addr = slot_addr & ~3u;
  uint32_t old_word  = *(const volatile uint32_t *)word_addr;
  uint32_t new_word  = old_word;

  if ((slot_addr & 2u) == 0) {
    // slot 在低半字：清零低 16bit
    new_word = old_word & 0xFFFF0000u;
  } else {
    // slot 在高半字：清零高 16bit
    new_word = old_word & 0x0000FFFFu;
  }

  nvmc_write_word(word_addr, new_word);
}

// ================= demo =================
static void do_trigger_and_print(const char *tag) {
  uint32_t f0 = g_fault_seen;

  // 关键：清零，避免“未触发 fault 但打印上次 fault 状态”
  g_fault_is_udf = 0;
  g_fault_target = 0;

  (void)vul_stub();

  uint32_t f1 = g_fault_seen;

  rtt_puts(tag);
  rtt_puts(" counter=");   rtt_put_u32(g_counter);
  rtt_puts(" enabled=");   rtt_putc(g_patch_enabled ? '1' : '0');
  rtt_puts(" patch_hit="); rtt_put_u32(g_patch_hit);
  rtt_puts(" fault+");     rtt_put_u32(f1 - f0);
  rtt_puts("\r\n");

  rtt_puts("      slot="); rtt_put_hex32(read_patch_slot_halfword());
  rtt_puts(" is_udf=");    rtt_put_u32(g_fault_is_udf);
  rtt_puts(" target=");    rtt_put_hex32(g_fault_target);
  rtt_puts("\r\n");
}

int main(void) {
  bsp_board_init(BSP_INIT_LEDS);

  SEGGER_RTT_Init();
  SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

  relocate_vtor_and_hook_faults();

  rtt_puts("\r\nRoute C (RTT) ready.\r\n");
  rtt_puts("Auto: t(off)->p->t(on)->o->t(off)->HARD UNINSTALL->t->halt\r\n");

  rtt_puts("\r\n[AUTO] start\r\n");

  // t1: patch off（插座为 UDF，fault+1，handler 分发到 original）
  g_patch_enabled = false;
  do_trigger_and_print("[AUTO] t1:");

  // p
  g_patch_enabled = true;
  rtt_puts("[AUTO] p : patch enabled\r\n");

  // t2: patch on（fault+1，handler 分发到 patch，counter 跳到 1001）
  do_trigger_and_print("[AUTO] t2:");

  // o
  g_patch_enabled = false;
  rtt_puts("[AUTO] o : patch disabled (still UDF socket)\r\n");

  // t3: patch off（仍 fault+1，但分发回 original）
  do_trigger_and_print("[AUTO] t3:");

  // 最后一步：把 UDF 插座写成合法指令（0x0000）
  rtt_puts("[AUTO] HARD UNINSTALL: program socket UDF->LEGAL in flash\r\n");
  hard_uninstall_socket_to_nop();
  rtt_puts("[AUTO] after hard uninstall, socket halfword=");
  rtt_put_hex32(read_patch_slot_halfword());
  rtt_puts("\r\n");

  // t4: 这次应 fault+0，且清零后 is_udf=0、target=0（不进入 fault handler）
  g_patch_enabled = true; // 即使开 patch 也无效，因为不再 fault
  do_trigger_and_print("[AUTO] t4 (no-fault):");

  rtt_puts("[AUTO] halt\r\n");
  while (1) { __WFE(); }
}
