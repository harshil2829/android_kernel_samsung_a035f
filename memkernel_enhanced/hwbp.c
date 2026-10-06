// language: C, file: memkernel_enhanced/hwbp.c, target: Linux ARM64 kernel module
#include "hwbp.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/ktime.h>
#include <asm/ptrace.h>

#define MAX_HITS 256

struct hwbp_entry {
    struct list_head list;
    pid_t pid;
    unsigned long addr;
    int type;
    int len;
    bool enabled;
    struct perf_event *pe;
    bool is_write_gp_regs;
    int  gp_reg_count;
    int  gp_reg_indices[MAX_MODIFY_REGS];
    u64  gp_reg_values[MAX_MODIFY_REGS];
    bool is_write_fp_regs;
    int  fp_reg_count;
    int  fp_reg_indices[MAX_MODIFY_REGS];
    u64  fp_reg_values[MAX_MODIFY_REGS][2];
    spinlock_t hits_lock;
    struct HWBP_HIT_ITEM hits[MAX_HITS];
    int hit_head;
    int hit_count;
};

static LIST_HEAD(hwbp_list);
static DEFINE_MUTEX(hwbp_mutex);

static struct task_struct *get_task_by_pid(pid_t pid)
{
    struct task_struct *task;
    rcu_read_lock();
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (task) get_task_struct(task);
    rcu_read_unlock();
    return task;
}

static void hwbp_callback(struct perf_event *bp, struct perf_sample_data *data,
                          struct pt_regs *regs)
{
    struct hwbp_entry *entry = bp->overflow_handler_context;
    struct HWBP_HIT_ITEM *hit;
    unsigned long flags;
    int i;

    if (!entry || !regs) return;

    if (entry->is_write_gp_regs) {
        for (i = 0; i < entry->gp_reg_count && i < MAX_MODIFY_REGS; i++) {
            int idx = entry->gp_reg_indices[i];
            if (idx >= 0 && idx < 31)
                regs->regs[idx] = entry->gp_reg_values[i];
        }
    }

    spin_lock_irqsave(&entry->hits_lock, flags);
    hit = &entry->hits[entry->hit_head];
    memset(hit, 0, sizeof(*hit));
    hit->task_id  = current->pid;
    hit->hit_addr = entry->addr;
    hit->hit_time = ktime_get_ns() / 1000000;
    for (i = 0; i < 31; i++) hit->regs_info.regs[i] = regs->regs[i];
    hit->regs_info.sp     = regs->sp;
    hit->regs_info.pc     = regs->pc;
    hit->regs_info.pstate = regs->pstate;
    entry->hit_head = (entry->hit_head + 1) % MAX_HITS;
    if (entry->hit_count < MAX_HITS) entry->hit_count++;
    spin_unlock_irqrestore(&entry->hits_lock, flags);
}

static struct perf_event *setup_hwbp(pid_t pid, unsigned long addr,
                                     int type, int len,
                                     struct hwbp_entry *entry)
{
    struct perf_event_attr attr;
    struct task_struct *task;
    struct perf_event *bp;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_BREAKPOINT;
    attr.size = sizeof(attr);
    attr.bp_addr = addr;

    switch (len) {
    case 1: attr.bp_len = HW_BREAKPOINT_LEN_1; break;
    case 2: attr.bp_len = HW_BREAKPOINT_LEN_2; break;
    case 4: attr.bp_len = HW_BREAKPOINT_LEN_4; break;
    case 8: attr.bp_len = HW_BREAKPOINT_LEN_8; break;
    default: attr.bp_len = HW_BREAKPOINT_LEN_4; break;
    }

    switch (type) {
    case HW_BP_TYPE_R:  attr.bp_type = HW_BREAKPOINT_R;  break;
    case HW_BP_TYPE_W:  attr.bp_type = HW_BREAKPOINT_W;  break;
    case HW_BP_TYPE_RW: attr.bp_type = HW_BREAKPOINT_RW; break;
    case HW_BP_TYPE_X:  attr.bp_type = HW_BREAKPOINT_X;  break;
    default: return NULL;
    }

    attr.sample_period = 1;
    attr.sample_type   = PERF_SAMPLE_IP;
    attr.disabled      = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv     = 1;

    task = get_task_by_pid(pid);
    if (!task) return NULL;
    bp = register_user_hw_breakpoint(&attr, hwbp_callback, entry, task);
    put_task_struct(task);
    return bp;
}

int hwbp_add(struct HW_BP_INFO *info)
{
    struct hwbp_entry *entry;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) return -ENOMEM;

    entry->pid     = info->pid;
    entry->addr    = info->addr;
    entry->type    = info->type;
    entry->len     = info->len;
    entry->enabled = false;
    spin_lock_init(&entry->hits_lock);

    entry->pe = setup_hwbp(info->pid, info->addr, info->type, info->len, entry);
    if (!entry->pe) { kfree(entry); return -EINVAL; }

    mutex_lock(&hwbp_mutex);
    list_add(&entry->list, &hwbp_list);
    mutex_unlock(&hwbp_mutex);
    return 0;
}

int hwbp_enable(struct HW_BP_INFO *info)
{
    struct hwbp_entry *entry;

    mutex_lock(&hwbp_mutex);
    list_for_each_entry(entry, &hwbp_list, list) {
        if (entry->pid != info->pid || entry->addr != info->addr)
            continue;

        entry->is_write_gp_regs = info->is_write_gp_regs;
        entry->gp_reg_count     = info->gp_reg_count;
        memcpy(entry->gp_reg_indices, info->gp_reg_indices,
               sizeof(entry->gp_reg_indices));
        memcpy(entry->gp_reg_values, info->gp_reg_values,
               sizeof(entry->gp_reg_values));
        entry->is_write_fp_regs = info->is_write_fp_regs;
        entry->fp_reg_count     = info->fp_reg_count;
        memcpy(entry->fp_reg_indices, info->fp_reg_indices,
               sizeof(entry->fp_reg_indices));
        memcpy(entry->fp_reg_values, info->fp_reg_values,
               sizeof(entry->fp_reg_values));

        if (entry->pe) perf_event_enable(entry->pe);
        entry->enabled = true;
        mutex_unlock(&hwbp_mutex);
        return 0;
    }
    mutex_unlock(&hwbp_mutex);
    return -ENOENT;
}

int hwbp_disable(pid_t pid, unsigned long addr)
{
    struct hwbp_entry *entry;

    mutex_lock(&hwbp_mutex);
    list_for_each_entry(entry, &hwbp_list, list) {
        if (entry->pid == pid && entry->addr == addr) {
            if (entry->pe) perf_event_disable(entry->pe);
            entry->enabled = false;
            mutex_unlock(&hwbp_mutex);
            return 0;
        }
    }
    mutex_unlock(&hwbp_mutex);
    return -ENOENT;
}

int hwbp_clear_all(void)
{
    struct hwbp_entry *entry, *tmp;

    mutex_lock(&hwbp_mutex);
    list_for_each_entry_safe(entry, tmp, &hwbp_list, list) {
        if (entry->pe) unregister_hw_breakpoint(entry->pe);
        list_del(&entry->list);
        kfree(entry);
    }
    mutex_unlock(&hwbp_mutex);
    return 0;
}

int hwbp_get_hits(struct HWBP_HIT_ARGS *args)
{
    struct hwbp_entry *entry;
    struct HWBP_HIT_ITEM *hits;
    unsigned long flags;
    int count, i, idx;

    mutex_lock(&hwbp_mutex);
    list_for_each_entry(entry, &hwbp_list, list) {
        if (entry->pid != args->pid || entry->addr != args->addr)
            continue;

        spin_lock_irqsave(&entry->hits_lock, flags);
        count = entry->hit_count;
        if (count > args->out_len / (int)sizeof(struct HWBP_HIT_ITEM))
            count = args->out_len / (int)sizeof(struct HWBP_HIT_ITEM);

        if (count == 0) {
            spin_unlock_irqrestore(&entry->hits_lock, flags);
            args->real_count = 0;
            mutex_unlock(&hwbp_mutex);
            return 0;
        }

        hits = kmalloc(count * sizeof(struct HWBP_HIT_ITEM), GFP_ATOMIC);
        if (!hits) {
            spin_unlock_irqrestore(&entry->hits_lock, flags);
            mutex_unlock(&hwbp_mutex);
            return -ENOMEM;
        }
        for (i = 0; i < count; i++) {
            idx = (entry->hit_head - count + i + MAX_HITS) % MAX_HITS;
            hits[i] = entry->hits[idx];
        }
        entry->hit_count = 0;
        entry->hit_head  = 0;
        spin_unlock_irqrestore(&entry->hits_lock, flags);

        if (copy_to_user(args->out_buf, hits,
                         count * sizeof(struct HWBP_HIT_ITEM))) {
            kfree(hits);
            mutex_unlock(&hwbp_mutex);
            return -EFAULT;
        }
        kfree(hits);

        args->real_count = count;
        mutex_unlock(&hwbp_mutex);
        return 0;
    }
    mutex_unlock(&hwbp_mutex);
    return -ENOENT;
}
