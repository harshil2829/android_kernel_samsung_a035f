#ifndef MEMKERNEL_ENHANCED_HWBP_H
#define MEMKERNEL_ENHANCED_HWBP_H

#include "comm.h"

int hwbp_add(struct HW_BP_INFO *info);
int hwbp_enable(struct HW_BP_INFO *info);
int hwbp_disable(pid_t pid, unsigned long addr);
int hwbp_clear_all(void);
int hwbp_get_hits(struct HWBP_HIT_ARGS *args);

#endif
