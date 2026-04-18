    .section .patchslot_erase, "ax", @progbits
    .align 2
    .globl patch_slot_erase
    .type patch_slot_erase, @function
    .globl patch_slot_erase_word
    .type patch_slot_erase_word, @object
    .globl patch_slot_erase_target
    .option push
    .option norvc
    .option norelax
patch_slot_erase:
patch_slot_erase_word:
    .word 0x02211063
    j fun1
    nop
    nop
    nop
    nop
    nop
    nop
patch_slot_erase_target:
    j fun2
    .size patch_slot_erase_word, 4
    .size patch_slot_erase, .-patch_slot_erase
    .option pop
