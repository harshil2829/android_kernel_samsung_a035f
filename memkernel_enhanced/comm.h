#ifndef MEMKERNEL_ENHANCED_COMM_H
#define MEMKERNEL_ENHANCED_COMM_H

#include <linux/types.h>

struct CopyMemory {
    pid_t pid;
    unsigned long addr;
    void *buffer;
    size_t size;
};

struct ModuleBase {
    pid_t pid;
    char *name;
    unsigned long base;
    short index;
};

enum Operations {
    OP_INIT_KEY            = 0x800,
    OP_READ_MEM            = 0x801,
    OP_WRITE_MEM           = 0x802,
    OP_MODULE_BASE         = 0x803,
    OP_CMD_HWBP_ADD        = 0x804,
    OP_CMD_HWBP_GET_HITS   = 0x805,
    OP_CMD_HWBP_ENABLE     = 0x806,
    OP_CMD_HWBP_CLEAR      = 0x807,
    OP_CMD_HWBP_DISABLE    = 0x809,
    OP_CMD_HIDE_PROCESS    = 0x810,
    OP_CMD_RECOVER_PROCESS = 0x811,
};

#define HW_BP_TYPE_R   1
#define HW_BP_TYPE_W   2
#define HW_BP_TYPE_RW  3
#define HW_BP_TYPE_X   4
#define MAX_MODIFY_REGS 10

struct HW_BP_INFO {
    pid_t pid;
    unsigned long addr;
    int type;
    int len;
    bool is_write_gp_regs;
    int gp_reg_count;
    int gp_reg_indices[MAX_MODIFY_REGS];
    u64 gp_reg_values[MAX_MODIFY_REGS];
    bool is_write_fp_regs;
    int fp_reg_count;
    int fp_reg_indices[MAX_MODIFY_REGS];
    u64 fp_reg_values[MAX_MODIFY_REGS][2];
};

struct REGS_INFO {
    u64 regs[31];
    u64 sp;
    u64 pc;
    u64 pstate;
};

struct HWBP_HIT_ITEM {
    pid_t task_id;
    unsigned long hit_addr;
    u64 hit_time;
    struct REGS_INFO regs_info;
};

struct HWBP_HIT_ARGS {
    pid_t pid;
    unsigned long addr;
    void *out_buf;
    int out_len;
    int real_count;
};

#endif
