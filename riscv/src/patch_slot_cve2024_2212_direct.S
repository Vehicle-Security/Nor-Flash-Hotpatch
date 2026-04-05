    .section .patchslot_2212, "ax", @progbits
    .align 2
    .globl patch_slot_cve2024_2212_direct
    .type patch_slot_cve2024_2212_direct, @function
    .globl patch_slot_cve2024_2212_direct_word
    .type patch_slot_cve2024_2212_direct_word, @object
    .globl patch_slot_cve2024_2212_direct_target
    .option push
    .option norvc
    .option norelax
patch_slot_cve2024_2212_direct:
patch_slot_cve2024_2212_direct_word:
    .word 0x02211063
    j fun1
    nop
    nop
    nop
    nop
    nop
    nop
patch_slot_cve2024_2212_direct_target:
    j fun2
    .size patch_slot_cve2024_2212_direct_word, 4
    .size patch_slot_cve2024_2212_direct, .-patch_slot_cve2024_2212_direct
    .option pop
