#ifndef MEMKERNEL_COMM_H
#define MEMKERNEL_COMM_H

struct CopyMemory
{
    pid_t pid;
    uintptr_t addr;
    void *buffer;
    size_t size;
};

struct ModuleBase
{
    pid_t pid;
    char *name;
    uintptr_t base;
    int index;
};

struct GetPfnReq
{
    pid_t pid;
    uintptr_t addr;
    uintptr_t pfn;      // output
    int32_t status;     // 0 = ok, <0 = error
};

enum Operations
{
    OP_READ_MEM = 0x801,
    OP_WRITE_MEM = 0x802,
    OP_MODULE_BASE = 0x803,
    OP_READ_MEM_APV = 0x804,
    OP_WRITE_MEM_APV = 0x805,
    OP_GET_PFN = 0x806,
};

#endif
