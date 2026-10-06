// language: C, file: memkernel_enhanced/entry.c, target: Linux ARM64 kernel module
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "comm.h"
#include "memory.h"
#include "process.h"
#include "hwbp.h"

#define DEVICE_NAME "mempdr"

static char g_key[0x100];
static bool g_key_set;

struct hidden_proc {
    struct list_head list;
    pid_t pid;
    char comm[TASK_COMM_LEN];
};
static LIST_HEAD(hidden_procs);
static DEFINE_MUTEX(hidden_mutex);

static int hide_process(void)
{
    struct hidden_proc *hp;
    struct task_struct *task = current;

    hp = kmalloc(sizeof(*hp), GFP_KERNEL);
    if (!hp) return -ENOMEM;
    hp->pid = task->pid;
    strncpy(hp->comm, task->comm, TASK_COMM_LEN);

    mutex_lock(&hidden_mutex);
    list_add(&hp->list, &hidden_procs);
    mutex_unlock(&hidden_mutex);

    strncpy(task->comm, "kworker/u8:0", TASK_COMM_LEN);
    return 0;
}

static int recover_process(void)
{
    struct hidden_proc *hp, *tmp;
    struct task_struct *task;

    mutex_lock(&hidden_mutex);
    list_for_each_entry_safe(hp, tmp, &hidden_procs, list) {
        rcu_read_lock();
        task = pid_task(find_vpid(hp->pid), PIDTYPE_PID);
        if (task) strncpy(task->comm, hp->comm, TASK_COMM_LEN);
        rcu_read_unlock();
        list_del(&hp->list);
        kfree(hp);
    }
    mutex_unlock(&hidden_mutex);
    return 0;
}

static int dispatch_open(struct inode *node, struct file *file) { return 0; }
static int dispatch_close(struct inode *node, struct file *file) { return 0; }

static long dispatch_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct CopyMemory cm;
    struct ModuleBase mb;
    char name[0x100] = {0};

    switch (cmd) {

    case OP_INIT_KEY:
        if (copy_from_user(g_key, (void __user *)arg, sizeof(g_key)))
            return -EFAULT;
        g_key_set = true;
        return 0;

    case OP_READ_MEM:
        if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)))
            return -1;
        return readwrite_process_memory(cm.pid, cm.addr, cm.buffer, cm.size, false);

    case OP_WRITE_MEM:
        if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)))
            return -1;
        return readwrite_process_memory(cm.pid, cm.addr, cm.buffer, cm.size, true);

    case OP_MODULE_BASE:
        if (copy_from_user(&mb, (void __user *)arg, sizeof(mb)))
            return -1;
        if (copy_from_user(name, (void __user *)mb.name, sizeof(name) - 1))
            return -1;
        mb.base = get_module_base(mb.pid, name, mb.index);
        if (copy_to_user((void __user *)arg, &mb, sizeof(mb)))
            return -1;
        return 0;

    case OP_CMD_HWBP_ADD: {
        struct HW_BP_INFO info;
        if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
            return -1;
        return hwbp_add(&info);
    }

    case OP_CMD_HWBP_ENABLE: {
        struct HW_BP_INFO info;
        if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
            return -1;
        return hwbp_enable(&info);
    }

    case OP_CMD_HWBP_DISABLE: {
        struct HW_BP_INFO info;
        if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
            return -1;
        return hwbp_disable(info.pid, info.addr);
    }

    case OP_CMD_HWBP_CLEAR:
        return hwbp_clear_all();

    case OP_CMD_HWBP_GET_HITS: {
        struct HWBP_HIT_ARGS args;
        int ret;
        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -1;
        ret = hwbp_get_hits(&args);
        if (ret < 0) return ret;
        if (copy_to_user((void __user *)arg, &args, sizeof(args)))
            return -EFAULT;
        return 0;
    }

    case OP_CMD_HIDE_PROCESS:
        return hide_process();

    case OP_CMD_RECOVER_PROCESS:
        return recover_process();

    default:
        return -ENOTTY;
    }
}

static struct file_operations dispatch_functions = {
    .owner          = THIS_MODULE,
    .open           = dispatch_open,
    .release        = dispatch_close,
    .unlocked_ioctl = dispatch_ioctl,
};

static struct miscdevice misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &dispatch_functions,
    .mode  = 0666,
};

static int __init memkernel_enhanced_entry(void)
{
    int ret;
    printk("[+] memkernel_enhanced driver loaded (Made by @rajput_harshil)\n");
    ret = misc_register(&misc);
    return ret;
}

static void __exit memkernel_enhanced_unload(void)
{
    printk("[+] memkernel_enhanced_unload (by @rajput_harshil)\n");
    hwbp_clear_all();
    recover_process();
    misc_deregister(&misc);
}

module_init(memkernel_enhanced_entry);
module_exit(memkernel_enhanced_unload);

MODULE_DESCRIPTION("memkernel_enhanced - Android ARM64 RT-Dev driver for SM-A035F");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("@rajput_harshil");
