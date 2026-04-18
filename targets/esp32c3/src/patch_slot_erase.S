    .section .text.patch_slot_erase, "ax", @progbits
    .balign 4096
    .globl patch_slot_erase
    .type patch_slot_erase, @function
    .globl patch_slot_erase_word
    .type patch_slot_erase_word, @object
    .globl patch_slot_erase_target
    .option push
    .option norvc
    .option norelax

sector_begin:
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

    /* Pad remainder of the 4 KB sector with 0xFF (erased flash). */
    .space (4096 - (. - sector_begin)), 0xff
    .option pop
