#include "rapidpatch_vm.h"

#include <limits.h>

typedef struct {
    uint8_t opcode;
    uint8_t regs;
    int16_t offset;
    int32_t imm;
} rapidpatch_inst_t;

enum {
    RAPIDPATCH_OP_LDDW      = 0x18,
    RAPIDPATCH_OP_ADD64_REG = 0x0Fu,
    RAPIDPATCH_OP_JEQ_IMM   = 0x15u,
    RAPIDPATCH_OP_LDXW      = 0x61u,
    RAPIDPATCH_OP_MUL32_REG = 0x2Cu,
    RAPIDPATCH_OP_MUL64_REG = 0x2Fu,
    RAPIDPATCH_OP_RSH64_IMM = 0x77u,
    RAPIDPATCH_OP_MOV64_IMM = 0xB7u,
    RAPIDPATCH_OP_EXIT      = 0x95u,
};

static uint8_t inst_dst(const rapidpatch_inst_t *inst) {
    return (uint8_t)(inst->regs & 0x0Fu);
}

static uint8_t inst_src(const rapidpatch_inst_t *inst) {
    return (uint8_t)((inst->regs >> 4) & 0x0Fu);
}

static bool load_is_in_bounds(uint64_t base, int16_t offset, size_t size, const uint8_t *ctx, size_t ctx_len) {
    uintptr_t addr = (uintptr_t)(base + (int32_t)offset);
    uintptr_t start = (uintptr_t)ctx;
    uintptr_t end = start + ctx_len;

    if (ctx == NULL || ctx_len == 0u) {
        return false;
    }

    if (addr < start) {
        return false;
    }

    if (size > (size_t)(end - addr)) {
        return false;
    }

    return true;
}

bool rapidpatch_vm_init(rapidpatch_vm_t *vm, const uint8_t *code, uint16_t code_len) {
    if (vm == NULL || code == NULL || code_len == 0u || (code_len % sizeof(rapidpatch_inst_t)) != 0u) {
        return false;
    }

    vm->code = code;
    vm->code_len = code_len;
    return true;
}

uint64_t rapidpatch_vm_exec(const rapidpatch_vm_t *vm, void *ctx, size_t ctx_len) {
    uint64_t regs[11] = {0};
    const rapidpatch_inst_t *insts;
    size_t inst_count;
    size_t pc = 0u;

    if (vm == NULL || vm->code == NULL || vm->code_len == 0u || ctx == NULL) {
        return UINT64_MAX;
    }

    insts = (const rapidpatch_inst_t *)vm->code;
    inst_count = (size_t)vm->code_len / sizeof(rapidpatch_inst_t);
    regs[1] = (uint64_t)(uintptr_t)ctx;

    while (pc < inst_count) {
        const rapidpatch_inst_t *inst = &insts[pc++];
        uint8_t dst = inst_dst(inst);
        uint8_t src = inst_src(inst);

        switch (inst->opcode) {
        case RAPIDPATCH_OP_MOV64_IMM:
            regs[dst] = (uint64_t)(uint32_t)inst->imm;
            break;

        case RAPIDPATCH_OP_LDXW:
            if (!load_is_in_bounds(regs[src], inst->offset, sizeof(uint32_t), (const uint8_t *)ctx, ctx_len)) {
                return UINT64_MAX;
            }
            regs[dst] = *(const uint32_t *)(uintptr_t)(regs[src] + (int32_t)inst->offset);
            break;

        case RAPIDPATCH_OP_LDDW:
            if (pc >= inst_count) {
                return UINT64_MAX;
            }
            regs[dst] =
                (uint64_t)(uint32_t)inst->imm |
                ((uint64_t)(uint32_t)insts[pc++].imm << 32);
            break;

        case RAPIDPATCH_OP_JEQ_IMM:
            if (regs[dst] == (uint64_t)(uint32_t)inst->imm) {
                pc = (size_t)((int32_t)pc + (int32_t)inst->offset);
            }
            break;

        case RAPIDPATCH_OP_MUL32_REG:
            regs[dst] = (uint64_t)((uint32_t)regs[dst] * (uint32_t)regs[src]);
            break;

        case RAPIDPATCH_OP_MUL64_REG:
            regs[dst] *= regs[src];
            break;

        case RAPIDPATCH_OP_RSH64_IMM:
            regs[dst] >>= (uint32_t)inst->imm;
            break;

        case RAPIDPATCH_OP_ADD64_REG:
            regs[dst] += regs[src];
            break;

        case RAPIDPATCH_OP_EXIT:
            return regs[0];

        default:
            return UINT64_MAX;
        }
    }

    return UINT64_MAX;
}
