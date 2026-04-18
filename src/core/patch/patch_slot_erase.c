/*
 * patch_slot_erase.c — Second patch slot for erase-rewrite benchmarking.
 *
 * Factory state: b.n +2 ; nop  (0xE7FF ; 0xBF00) — falls through to fun1.
 * The erase scheme overwrites the full word with a B.W (32-bit branch to fun2).
 */

__attribute__((naked, noinline, used, section(".erase_patchslot.slot"), aligned(4)))
int patch_slot_erase(void)
{
    __asm volatile(
        ".thumb          \n"
        ".hword 0xE7FF   \n"   /* b.n +2 (skip nop, fall to b fun1) */
        ".hword 0xBF00   \n"   /* nop (padding for 32-bit B.W replacement) */
        "b       fun1    \n");
}
