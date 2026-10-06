// language: C, file: memkernel/memory.c, target: Linux ARM64 kernel module
// *read/write physical address now go through memremap(MEMREMAP_WB) — the
//  correct API for mapping System RAM. Returns the kernel's direct-map
//  pointer for lowmem pages, which shares Normal-WB attributes with the
//  target process's own user mapping, so ARMv8 hardware cache coherency
//  propagates writes to the target's cores.*
#include "memory.h"
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))
#include <linux/mmap_lock.h>
#define MM_READ_LOCK(mm) mmap_read_lock(mm);
#define MM_READ_UNLOCK(mm) mmap_read_unlock(mm);
#else
#include <linux/rwsem.h>
#define MM_READ_LOCK(mm) down_read(&(mm)->mmap_sem);
#define MM_READ_UNLOCK(mm) up_read(&(mm)->mmap_sem);
#endif

/* Retry tuning: 120 attempts * ~8us with cond_resched between = ~1ms window.
 * Long enough for the game's own execution to fault a transiently-unmapped
 * page back in, short enough that a genuinely invalid address doesn't stall. */
#define PTE_RETRY_MAX     120
#define PTE_RETRY_UDELAY  8


static phys_addr_t translate_linear_address(struct mm_struct *mm, uintptr_t va)
{
    pgd_t *pgd;
#ifdef __PAGETABLE_P4D_FOLDED
    p4d_t *p4d;
#endif
    pmd_t *pmd;
    pte_t *pte;
    pud_t *pud;
    pte_t pte_val;

    phys_addr_t page_addr;
    uintptr_t page_offset;

    pgd = pgd_offset(mm, va);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return 0;
    }
#ifdef __PAGETABLE_P4D_FOLDED
    p4d = p4d_offset(pgd, va);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        return 0;
    }
    pud = pud_offset(p4d, va);
#else
    pud = pud_offset(pgd, va);
#endif
    if (pud_none(*pud) || pud_bad(*pud)) {
        return 0;
    }
    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd)) {
        return 0;
    }

    pte = pte_offset_kernel(pmd, va);
    if (!pte) {
        return 0;
    }

    /* Single atomic PTE snapshot — kills the "BOT then real name" flip
     * that happened when the PTE was read three times and the kernel
     * remapped it between reads. */
    pte_val = READ_ONCE(*pte);
    if (pte_none(pte_val)) {
        return 0;
    }
    if (!pte_present(pte_val)) {
        return 0;
    }
    page_addr = (phys_addr_t)(pte_pfn(pte_val) << PAGE_SHIFT);
    page_offset = va & (PAGE_SIZE - 1);

    return page_addr + page_offset;
}


/* Retry wrapper — the manual walk with a short sleep between attempts so
 * the game's own execution can fault the page back in. Lock is released
 * between attempts to unblock the game's own mm operations. */
static phys_addr_t translate_linear_address_retry(struct mm_struct *mm, uintptr_t va)
{
    phys_addr_t pa = 0;
    int i;

    for (i = 0; i < PTE_RETRY_MAX; i++) {
        MM_READ_LOCK(mm);
        pa = translate_linear_address(mm, va);
        MM_READ_UNLOCK(mm);
        if (pa) {
            return pa;
        }
        /* Yield to game threads so they can fault the page back in */
        cond_resched();
        udelay(PTE_RETRY_UDELAY);
    }
    return 0;
}


#if !defined(ARCH_HAS_VALID_PHYS_ADDR_RANGE) || defined(MODULE)
static inline int memk_valid_phys_addr_range(phys_addr_t addr, size_t size)
{
    return addr + size <= __pa(high_memory);
}
#define IS_VALID_PHYS_ADDR_RANGE(x,y) memk_valid_phys_addr_range(x,y)
#else
#define IS_VALID_PHYS_ADDR_RANGE(x,y) valid_phys_addr_range(x,y)
#endif

/* Read physical memory into a userspace buffer.
 *
 * memremap(..., MEMREMAP_WB) is the kernel's correct API for System RAM.
 * For lowmem pages (which game heaps always are) it returns a pointer into
 * the kernel's existing linear map — the same canonical Normal-WB mapping
 * that shares attributes with the target process's user mapping. ARMv8
 * hardware cache coherency is defined for this case.
 *
 * The previous ioremap_cache() created a second, distinct mapping whose
 * attributes were not guaranteed to match the target's. When they didn't,
 * coherency broke silently: reads via the driver looked correct, but the
 * target core kept returning its stale L1 line.
 */
static size_t read_physical_address(phys_addr_t pa, void *buffer, size_t size)
{
    void *mapped;

    if (!pfn_valid(__phys_to_pfn(pa))) {
        return 0;
    }
    if (!IS_VALID_PHYS_ADDR_RANGE(pa, size)) {
        return 0;
    }

    mapped = memremap(pa, size, MEMREMAP_WB);
    if (!mapped) {
        return 0;
    }
    if (copy_to_user(buffer, mapped, size)) {
        memunmap(mapped);
        return 0;
    }
    memunmap(mapped);
    return size;
}

/* Write a userspace buffer to physical memory.
 *
 * Symmetric to read_physical_address. Writing through the direct-map
 * pointer returned by memremap(MEMREMAP_WB) lets ARMv8 coherency push the
 * dirty line to every core with the page mapped. This is the fix for the
 * silent-aim write pattern: previous ioremap_cache writes landed in DRAM
 * but never reached the game's cores.
 */
static size_t write_physical_address(phys_addr_t pa, void *buffer, size_t size)
{
    void *mapped;

    if (!pfn_valid(__phys_to_pfn(pa))) {
        return 0;
    }
    if (!IS_VALID_PHYS_ADDR_RANGE(pa, size)) {
        return 0;
    }

    mapped = memremap(pa, size, MEMREMAP_WB);
    if (!mapped) {
        return 0;
    }
    if (copy_from_user(mapped, buffer, size)) {
        memunmap(mapped);
        return 0;
    }
    memunmap(mapped);
    return size;
}

ssize_t readwrite_process_memory(
    pid_t pid,
    uintptr_t addr,
    void *buffer,
    size_t size,
    bool iswrite)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct pid *pid_struct;
    phys_addr_t pa;
    size_t max_chunk;
    size_t count = 0;

    if (size <= 0 || buffer == NULL) {
        return -1;
    }

    pid_struct = find_get_pid(pid);
    if (!pid_struct) {
        return -1;
    }
    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    if (!task) {
        return -1;
    }
    mm = get_task_mm(task);
    put_task_struct(task);
    if (!mm) {
        return -1;
    }

    while (size > 0) {
        pa = translate_linear_address_retry(mm, addr);
        if (!pa) {
            break;
        }

        max_chunk = min(PAGE_SIZE - (addr & (PAGE_SIZE - 1)), min(size, PAGE_SIZE));

        if (iswrite
            ? !write_physical_address(pa, buffer, max_chunk)
            : !read_physical_address(pa, buffer, max_chunk))
        {
            break;
        }

        count += max_chunk;
        size -= max_chunk;
        buffer += max_chunk;
        addr += max_chunk;
    }
    mmput(mm);
    return (count > 0 ? count : -1);
}

/*
 * access_process_vm path — same function /proc/<pid>/mem uses internally.
 * Faults pages in via handle_mm_fault, no /proc file opens from userspace.
 * Exported by Samsung's 4.19 kernel to loadable modules.
 */
ssize_t readwrite_process_memory_apv(
    pid_t pid,
    uintptr_t addr,
    void *buffer,
    size_t size,
    bool iswrite)
{
    struct task_struct *task;
    struct pid *pid_struct;
    unsigned int gup_flags;
    void *kbuf;
    int ret;

    if (size <= 0 || buffer == NULL || size > 0x100000) {
        return -1;
    }

    pid_struct = find_get_pid(pid);
    if (!pid_struct) {
        return -1;
    }

    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    if (!task) {
        return -1;
    }

    kbuf = kvmalloc(size, GFP_KERNEL);
    if (!kbuf) {
        put_task_struct(task);
        return -1;
    }

    gup_flags = FOLL_FORCE;
    if (iswrite) {
        if (copy_from_user(kbuf, buffer, size) != 0) {
            kvfree(kbuf);
            put_task_struct(task);
            return -1;
        }
        gup_flags |= FOLL_WRITE;
    }

    ret = access_process_vm(task, (unsigned long)addr, kbuf, (int)size, gup_flags);
    put_task_struct(task);

    if (ret > 0 && !iswrite) {
        if (copy_to_user(buffer, kbuf, ret) != 0) {
            kvfree(kbuf);
            return -1;
        }
    }

    kvfree(kbuf);
    return ret > 0 ? ret : -1;
}

/* Returns the page frame number for the page containing `addr` in `pid`.
 * Returns -1 on failure. Uses the manual walk — no fault handler. */
long get_process_pfn(pid_t pid, uintptr_t addr)
{
    struct task_struct *task;
    struct mm_struct *mm;
    struct pid *pid_struct;
    phys_addr_t pa;

    if (!pid || !addr) return -1;

    pid_struct = find_get_pid(pid);
    if (!pid_struct) return -1;

    task = get_pid_task(pid_struct, PIDTYPE_PID);
    put_pid(pid_struct);
    if (!task) return -1;

    mm = get_task_mm(task);
    put_task_struct(task);
    if (!mm) return -1;

    MM_READ_LOCK(mm);
    pa = translate_linear_address(mm, addr & PAGE_MASK);
    MM_READ_UNLOCK(mm);
    mmput(mm);

    if (!pa) return -1;
    if (!pfn_valid(__phys_to_pfn(pa))) return -1;

    return (long)__phys_to_pfn(pa);
}
