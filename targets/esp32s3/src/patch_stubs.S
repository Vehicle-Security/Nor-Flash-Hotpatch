#
# patch_stubs.S -- Xtensa patch slot stubs for MorphPatch (ESP32-S3).
#
# Morph slot: uses bnall/ball/bnone a15,a15 (3-byte branch instructions).
# Erase slot: starts as NOP (or a15,a15,a15), sector-aligned for erase-rewrite.
#
# fun1() = vulnerable path (no arguments, returns int)
# fun2() = fixed path      (no arguments, returns int)
#

    # ---------- Morph patch slot ----------

    .section .literal.patch_entry, "a", @progbits
    .align 4
    .global patch_target_literal
    .type patch_target_literal, @object
patch_target_literal:
    .word fun2
    .size patch_target_literal, 4

    .section .text.patch_entry, "ax", @progbits
    .align 4

    .global patch_slot_call
    .type patch_slot_call, @function
patch_slot_call:
    entry a1, 32
    movi a7, 1
    call8 patch_entry
    mov.n a2, a10
    retw.n
    .size patch_slot_call, .-patch_slot_call

    .align 4
    .global patch_entry
    .type patch_entry, @function
    .global patch_slot_begin
    .type patch_slot_begin, @object
    .global trampoline_begin
    .type trampoline_begin, @function
patch_entry:
patch_slot_begin:
    bnall a15, a15, trampoline_begin

fallthrough_vul:
    j fun1

    .align 4
trampoline_begin:
    l32r a13, patch_target_literal
    jx a13
    .size patch_slot_begin, 3
    .size patch_entry, .-patch_entry

    # ---------- Erase-rewrite patch slot ----------

    .section .text.patch_entry_erase_call, "ax", @progbits
    .align 4

    .global patch_slot_erase_call
    .type patch_slot_erase_call, @function
patch_slot_erase_call:
    entry a1, 32
    movi a7, 1
    call8 patch_entry_erase
    mov.n a2, a10
    retw.n
    .size patch_slot_erase_call, .-patch_slot_erase_call

    .set PATCH_ERASE_BENCH_SECTOR_BYTES, 4096
    .section .text.patch_entry_erase_sector, "ax", @progbits
    .balign 4096

    .global patch_slot_erase_sector_begin
    .type patch_slot_erase_sector_begin, @object
patch_slot_erase_sector_begin:
    .align 4
    .global patch_target_literal_erase
    .type patch_target_literal_erase, @object
patch_target_literal_erase:
    .word fun2
    .size patch_target_literal_erase, 4

    .align 4
    .global patch_entry_erase
    .type patch_entry_erase, @function
    .global patch_slot_erase_begin
    .type patch_slot_erase_begin, @object
    .global trampoline_erase_begin
    .type trampoline_erase_begin, @function
patch_entry_erase:
patch_slot_erase_begin:
    .begin no-transform
    or a15, a15, a15
    .end no-transform

fallthrough_vul_erase:
    j fun1

    .align 4
trampoline_erase_begin:
    l32r a13, patch_target_literal_erase
    jx a13
    .size patch_slot_erase_begin, 3
    .size patch_entry_erase, .-patch_entry_erase
    .space PATCH_ERASE_BENCH_SECTOR_BYTES - (. - patch_slot_erase_sector_begin), 0xff
