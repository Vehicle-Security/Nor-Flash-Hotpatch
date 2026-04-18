    .section .text.patch_slot, "ax", @progbits
    .align 2
    .globl patch_slot
    .type patch_slot, @function
    .globl patch_slot_word
    .type patch_slot_word, @object
    .option push
    .option norvc
    .option norelax
patch_slot:
patch_slot_word:
    .word 0x02211063
    j fun1
    nop
    nop
    nop
    nop
    nop
    nop
patch_slot_target:
    j fun2
    .size patch_slot_word, 4
    .size patch_slot, .-patch_slot
    .option pop
