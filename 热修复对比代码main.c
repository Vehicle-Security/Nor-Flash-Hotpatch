
/*
 * nRF52840: Hotfix total-cost benchmark for multiple hit counts N
 *
 * Goal:
 *   For each "safe" target 16-bit Thumb instruction halfword (hw),
 *   compare TOTAL COST for N hits where N ∈ {1,10,50,100,500,1000}:
 *
 *     A) Fault-based reversible hotfix:
 *        Start: slot in flash contains UDF (invalid) and stays that way.
 *        Apply: enable fault redirect + write RAM thunk = [hw][BX LR]
 *        Hits : call slot N times (each hit faults, redirects, executes hw in RAM)
 *        Restore: disable redirect + reset thunk to NOP
 *        End: slot still UDF, patch disabled (same as start)
 *
 *     B) Traditional flash hotfix (erase+rewrite):
 *        Start: slot in flash contains UDF (invalid)
 *        Apply: ERASE page + write slot = [hw][BX LR]
 *        Hits : call slot N times (no fault; directly executes hw)
 *        Restore: ERASE page + write slot = [UDF][BX LR]
 *        End: slot UDF again (same as start)
 *
 * Notes / Safety:
 *   - This program erases the BENCH FLASH PAGE many times:
 *       per hw per N: B does 2 page erases.
 *       With ~72 hw and 6 N values => ~864 page erases per full run.
 *     Ensure you are using a sacrificial / unused flash page.
 *   - Disable "break on exceptions" in debugger (UsageFault/HardFault will be frequent).
 *   - If you use SoftDevice, do NOT use NVMC directly; use sd_flash_* APIs instead.
 *
 * Build environment assumptions:
 *   - nRF5 SDK-style headers (nrf.h, bsp.h, SEGGER_RTT.h).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "nrf.h"
#include "bsp.h"
#include "SEGGER_RTT.h"

// =====================================================
// RTT minimal output (avoid heavy printf)
// =====================================================
static void rtt_putc(char c) { SEGGER_RTT_PutChar(0, c); }
static void rtt_puts(const char *s) { SEGGER_RTT_WriteString(0, s); }

static void rtt_put_u32(uint32_t v) {
  char buf[11];
  int i = 10;
  buf[i--] = '\0';
  if (v == 0) { buf[i] = '0'; rtt_puts(&buf[i]); return; }
  while (v && i >= 0) { buf[i--] = (char)('0' + (v % 10u)); v /= 10u; }
  rtt_puts(&buf[i + 1]);
}

static void rtt_put_hex16(uint16_t v) {
  static const char hex[] = "0123456789ABCDEF";
  rtt_puts("0x");
  for (int i = 3; i >= 0; --i) rtt_putc(hex[(v >> (i * 4)) & 0xF]);
}

static void rtt_put_hex32(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  rtt_puts("0x");
  for (int i = 7; i >= 0; --i) rtt_putc(hex[(v >> (i * 4)) & 0xF]);
}

// Very small printf subset: %s %c %u
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

// =====================================================
// DWT cycle counter
// =====================================================
static void dwt_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t cyc_now(void) { return DWT->CYCCNT; }

static inline uint32_t cycles_to_us(uint32_t cycles) {
  uint32_t hz = SystemCoreClock;
  if (hz == 0) return 0;
  return (uint32_t)(((uint64_t)cycles * 1000000ull) / (uint64_t)hz);
}

// =====================================================
// NVMC flash write/erase (direct, no SoftDevice)
// =====================================================
static inline void nvmc_wait_ready(void) {
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

// Keep these noinline to avoid LTO weirdness.
__attribute__((noinline))
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

__attribute__((noinline))
static void nvmc_erase_page(uint32_t page_addr) {
  __disable_irq();

  nvmc_wait_ready();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
  nvmc_wait_ready();

  NRF_NVMC->ERASEPAGE = page_addr;
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

// =====================================================
// Bench page and slot layout
// =====================================================
static uint32_t flash_page_size(void) { return NRF_FICR->CODEPAGESIZE; }
static uint32_t flash_total_size(void) { return NRF_FICR->CODEPAGESIZE * NRF_FICR->CODESIZE; }

static uint32_t bench_page_addr(void) {
  // Default: last page
  uint32_t ps = flash_page_size();
  uint32_t fs = flash_total_size();
  return (fs - ps);
}

#define BASE_UDF_HW   (0xDE42u)
#define BXLR_HW       (0x4770u)

static inline uint32_t slot_addr(uint32_t page) { return page; }
static inline uint32_t slot_word_addr(uint32_t page) { return page; }
static inline uint16_t slot_read_hw(uint32_t page) { return (uint16_t)(*(volatile uint32_t*)slot_word_addr(page) & 0xFFFFu); }
static inline uint32_t slot_make_word(uint16_t hw) { return ((uint32_t)BXLR_HW << 16) | (uint32_t)hw; }

static bool slot_set_udf_with_erase(uint32_t page) {
  nvmc_erase_page(page);
  nvmc_write_word(slot_word_addr(page), slot_make_word((uint16_t)BASE_UDF_HW));
  return slot_read_hw(page) == (uint16_t)BASE_UDF_HW;
}

static bool slot_set_hw_with_erase(uint32_t page, uint16_t hw) {
  nvmc_erase_page(page);
  nvmc_write_word(slot_word_addr(page), slot_make_word(hw));
  return slot_read_hw(page) == hw;
}

// =====================================================
// Fault-based hotfix mechanism
// =====================================================
typedef int (*fn0_t)(void);

// RAM thunk executes: [target_hw][bx lr]
__attribute__((aligned(4)))
static uint16_t g_thunk_code[2];

static volatile uint8_t  g_patch_enabled = 0;
static volatile uint32_t g_last_status = 0;   // 0=none,1=INVALID_FAULT,2=PATCH_OK
static volatile uint32_t g_fault_count = 0;

static void thunk_set_hw(uint16_t hw) {
  g_thunk_code[0] = hw;
  g_thunk_code[1] = (uint16_t)BXLR_HW;
  __DSB(); __ISB();
}

static inline int call_slot(uint32_t page) {
  g_last_status = 0;
  return ((fn0_t)((slot_addr(page) | 1u)))();
}

// =====================================================
// Vector table relocation + fault handlers
// =====================================================
#define VTOR_ENTRIES (16u + 48u)
__attribute__((aligned(256)))
static uint32_t g_vtor_ram[VTOR_ENTRIES];

static void fault_dispatch(uint32_t *stacked) {
  g_fault_count++;

  uint32_t spc = stacked[6];
  uint32_t pc  = spc & ~1u;

  uint32_t page = bench_page_addr();
  uint32_t slot = slot_addr(page);

  // Handle only faults from slot function
  if (pc == slot || pc == (slot + 2u)) {
    if (g_patch_enabled) {
      g_last_status = 2; // PATCH_OK
      stacked[6] = ((uint32_t)g_thunk_code) | 1u;
    } else {
      g_last_status = 1; // INVALID_FAULT
      // skip invalid instruction; return at BX LR
      stacked[6] = (slot + 2u) | 1u;
    }
    // clear sticky flags
    SCB->CFSR = SCB->CFSR;
    SCB->HFSR = SCB->HFSR;
    return;
  }

  // Fallback: clear and return to LR
  SCB->CFSR = SCB->CFSR;
  SCB->HFSR = SCB->HFSR;
  stacked[6] = (stacked[5] | 1u);
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

static void relocate_vtor_and_hook_faults(void) {
  uint32_t *vtor_flash = (uint32_t *)SCB->VTOR;
  if (vtor_flash == 0) vtor_flash = (uint32_t *)0x00000000u;

  for (uint32_t i = 0; i < VTOR_ENTRIES; i++) g_vtor_ram[i] = vtor_flash[i];

  g_vtor_ram[3] = (uint32_t)&MyHardFault;   // HardFault
  g_vtor_ram[6] = (uint32_t)&MyUsageFault;  // UsageFault

  SCB->VTOR = (uint32_t)g_vtor_ram;
  __DSB(); __ISB();

  // Enable UsageFault (UDF triggers UsageFault)
  SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
  __DSB(); __ISB();
}

// =====================================================
// Target selection (safe subset)
// =====================================================
// "Executable safe" here means we only allow:
//   - 0x0000..0x1FFF  (Thumb16 shift/imm/addsub family)
//   - 0x4000..0x43FF  (Thumb16 data-processing register family, limited)
// Additionally, to keep the list small and consistent with earlier runs,
// we only enumerate values reachable by clearing bits from BASE_UDF_HW:
//   (hw & ~BASE_UDF_HW) == 0
static bool is_exec_safe_target(uint16_t hw) {
  if (hw < 0x2000u) return true;
  if ((hw & 0xFC00u) == 0x4000u) return true; // 0x4000..0x43FF
  return false;
}

// =====================================================
// Measurement helpers
// =====================================================
static inline void barrier(void) { __DSB(); __ISB(); }

static bool ensure_start_state(uint32_t page) {
  // Patch disabled and thunk NOP
  g_patch_enabled = 0;
  thunk_set_hw(0x0000u);
  barrier();

  // Slot should be UDF in flash
  if (slot_read_hw(page) != (uint16_t)BASE_UDF_HW) {
    if (!slot_set_udf_with_erase(page)) return false;
  }
  return true;
}

static bool verify_invalid_once(uint32_t page) {
  uint32_t f0 = g_fault_count;
  (void)call_slot(page);
  uint32_t f1 = g_fault_count;
  return (g_last_status == 1u) && (f1 == (f0 + 1u));
}

static bool verify_fault_patch_once(uint32_t page, uint16_t hw) {
  thunk_set_hw(hw);
  g_patch_enabled = 1;
  barrier();
  uint32_t f0 = g_fault_count;
  (void)call_slot(page);
  uint32_t f1 = g_fault_count;
  // One fault should occur and status should be PATCH_OK
  return (g_last_status == 2u) && (f1 == (f0 + 1u));
}

static bool verify_flash_patch_once(uint32_t page) {
  uint32_t f0 = g_fault_count;
  (void)call_slot(page);
  uint32_t f1 = g_fault_count;
  // No fault expected; g_last_status stays 0
  return (g_last_status == 0u) && (f1 == f0);
}

static bool measure_A_total(uint32_t page, uint16_t hw, uint32_t N, uint32_t *out_cycles) {
  if (!ensure_start_state(page)) return false;
  if (!verify_invalid_once(page)) return false;

  // Apply hotfix (enable redirect + set thunk), hit N times, restore (disable)
  uint32_t t0 = cyc_now();

  thunk_set_hw(hw);
  g_patch_enabled = 1;
  barrier();

  // Optional: verify once outside timing? would distort small-N. We skip and verify via fault count & last status.
  uint32_t f0 = g_fault_count;
  for (uint32_t i = 0; i < N; i++) {
    (void)call_slot(page);
  }
  uint32_t f1 = g_fault_count;

  g_patch_enabled = 0;
  thunk_set_hw(0x0000u);
  barrier();

  uint32_t t1 = cyc_now();
  *out_cycles = (t1 - t0);

  // Check hits: must have N faults and last status should be PATCH_OK (from last hit)
  if ((f1 - f0) != N) return false;
  if (N > 0 && g_last_status != 2u) return false;

  // End state must be invalid again
  if (!verify_invalid_once(page)) return false;

  return true;
}

static bool measure_B_total(uint32_t page, uint16_t hw, uint32_t N, uint32_t *out_cycles) {
  if (!ensure_start_state(page)) return false;
  if (!verify_invalid_once(page)) return false;

  uint32_t t0 = cyc_now();

  // Apply: erase+write target
  if (!slot_set_hw_with_erase(page, hw)) return false;

  // Hits: should NOT fault
  uint32_t f0 = g_fault_count;
  for (uint32_t i = 0; i < N; i++) {
    (void)call_slot(page);
  }
  uint32_t f1 = g_fault_count;

  // Restore: erase+write UDF
  if (!slot_set_udf_with_erase(page)) return false;

  uint32_t t1 = cyc_now();
  *out_cycles = (t1 - t0);

  // Check: no faults during hits
  if ((f1 - f0) != 0u) return false;

  // End state must be invalid again
  if (!ensure_start_state(page)) return false;
  if (!verify_invalid_once(page)) return false;

  return true;
}

static void print_ratio3(uint32_t num, uint32_t den) {
  if (den == 0u) { rtt_puts("inf"); return; }
  uint64_t r1000 = ((uint64_t)num * 1000ull + (uint64_t)den / 2ull) / (uint64_t)den;
  rtt_put_u32((uint32_t)(r1000 / 1000ull));
  rtt_putc('.');
  uint32_t frac = (uint32_t)(r1000 % 1000ull);
  rtt_putc((char)('0' + (frac / 100u)));
  rtt_putc((char)('0' + ((frac / 10u) % 10u)));
  rtt_putc((char)('0' + (frac % 10u)));
}

static void run_benchmark(uint32_t page) {
  static const uint32_t Ns[] = {1, 10, 100, 1000, 10000, 50000, 60000, 64000, 65000, 66000, 100000};

  rtt_puts("\r\n=== nRF52840 Hotfix Total-Cost Sweep (ARMv7-M) ===\r\n");
  rtt_puts("SystemCoreClock="); rtt_put_u32(SystemCoreClock); rtt_puts(" Hz\r\n");
  rtt_puts("flash page_size="); rtt_put_u32(flash_page_size());
  rtt_puts(" total="); rtt_put_u32(flash_total_size());
  rtt_puts(" bench_page_addr="); rtt_put_hex32(page); rtt_puts("\r\n");
  rtt_puts("slot_addr="); rtt_put_hex32(slot_addr(page));
  rtt_puts(" base(UDF)="); rtt_put_hex16((uint16_t)BASE_UDF_HW); rtt_puts("\r\n");
  rtt_puts("N set: {1,10,100,1000,10000,50000,60000,64000,65000,66000,100000}\r\n");
  rtt_puts("A=fault-based reversible (enable+RAM thunk; per-hit faults)\r\n");
  rtt_puts("B=flash hotfix (erase+write target; per-hit no fault; erase+write UDF)\r\n");
  rtt_puts("NOTE: disable 'break on exceptions' in debugger.\r\n");
  rtt_puts("WARNING: many page erases on bench page.\r\n\r\n");

  // Initialize slot to base UDF
  if (!slot_set_udf_with_erase(page)) {
    rtt_puts("ERR: cannot init slot to UDF\r\n");
    return;
  }

  uint32_t ok_rows = 0, fail_rows = 0, skipped = 0;

  // Enumerate: (hw & ~BASE_UDF_HW)==0 AND safe exec range
  for (uint32_t cand = 0; cand <= 0xFFFFu; cand++) {
    uint16_t hw = (uint16_t)cand;
    if ((hw & (uint16_t)(~(uint16_t)BASE_UDF_HW)) != 0) continue;
    if (hw == (uint16_t)BASE_UDF_HW) continue;
    if (!is_exec_safe_target(hw)) { skipped++; continue; }

    // For each N, measure total cost for A and B
    for (uint32_t ni = 0; ni < (sizeof(Ns) / sizeof(Ns[0])); ni++) {
      uint32_t N = Ns[ni];

      // Ensure base state before each pair
      if (!ensure_start_state(page)) {
        fail_rows++;
        rtt_puts("FAIL(init) hw="); rtt_put_hex16(hw);
        rtt_puts(" N="); rtt_put_u32(N); rtt_puts("\r\n");
        continue;
      }

      uint32_t a_cyc = 0, b_cyc = 0;
      bool okA = measure_A_total(page, hw, N, &a_cyc);
      bool okB = measure_B_total(page, hw, N, &b_cyc);

      if (!okA || !okB) {
        fail_rows++;
        rtt_puts("FAIL hw="); rtt_put_hex16(hw);
        rtt_puts(" N="); rtt_put_u32(N);
        rtt_puts(" okA="); rtt_put_u32(okA ? 1u : 0u);
        rtt_puts(" okB="); rtt_put_u32(okB ? 1u : 0u);
        rtt_puts(" slot="); rtt_put_hex16(slot_read_hw(page));
        rtt_puts("\r\n");

        // attempt to restore base state for next iterations
        (void)slot_set_udf_with_erase(page);
        g_patch_enabled = 0;
        thunk_set_hw(0x0000u);
        barrier();
        continue;
      }

      ok_rows++;

      rtt_puts("hw="); rtt_put_hex16(hw);
      rtt_puts(" N="); rtt_put_u32(N);

      rtt_puts("  A_total="); rtt_put_u32(a_cyc); rtt_puts("cyc/"); rtt_put_u32(cycles_to_us(a_cyc)); rtt_puts("us");
      rtt_puts("  B_total="); rtt_put_u32(b_cyc); rtt_puts("cyc/"); rtt_put_u32(cycles_to_us(b_cyc)); rtt_puts("us");

      rtt_puts("  ratio(B/A)="); print_ratio3(b_cyc, a_cyc);
      rtt_puts("\r\n");
    }
  }

  rtt_puts("\r\n[SUMMARY] ok_rows="); rtt_put_u32(ok_rows);
  rtt_puts(" fail_rows="); rtt_put_u32(fail_rows);
  rtt_puts(" skipped_hw="); rtt_put_u32(skipped);
  rtt_puts("\r\n");
}

int main(void) {
  bsp_board_init(BSP_INIT_LEDS);

  SEGGER_RTT_Init();
  SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

  SystemCoreClockUpdate();
  dwt_init();
  relocate_vtor_and_hook_faults();

  uint32_t page = bench_page_addr();
  run_benchmark(page);

  rtt_puts("\r\n[END] halt\r\n");
  while (1) { __WFE(); }
}
