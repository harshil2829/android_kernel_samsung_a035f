// MIT License
/*
 * Memory Operation driver for Linux Android
 *
 * Original author:  Jiang-Night
 * Current maintainer: Poko
 *
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include "comm.h"
#include "memory.h"
#include "process.h"

#define DEVICE_NAME "phmeop"

static int dispatch_open(struct inode *node, struct file *file) {
	return 0;
}

static int dispatch_close(struct inode *node, struct file *file) {
	return 0;
}

static long dispatch_ioctl(struct file *const file, unsigned int const cmd, unsigned long const arg)
{
	struct CopyMemory cm;
	struct ModuleBase mb;
	char name[0x100] = {0};

	switch (cmd)
	{
	case OP_READ_MEM:
	{
		if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)) != 0) {
			return -1;
		}
		return readwrite_process_memory(cm.pid, cm.addr, cm.buffer, cm.size, false);
	}
	case OP_WRITE_MEM:
	{
		if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)) != 0) {
			return -1;
		}
		return readwrite_process_memory(cm.pid, cm.addr, cm.buffer, cm.size, true);
	}
	case OP_READ_MEM_APV:
	{
		if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)) != 0) {
			return -1;
		}
		return readwrite_process_memory_apv(cm.pid, cm.addr, cm.buffer, cm.size, false);
	}
	case OP_WRITE_MEM_APV:
	{
		if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)) != 0) {
			return -1;
		}
		return readwrite_process_memory_apv(cm.pid, cm.addr, cm.buffer, cm.size, true);
	}
	case OP_GET_PFN:
	{
		struct GetPfnReq req;
		long pfn;
		if (copy_from_user(&req, (void __user *)arg, sizeof(req)) != 0) {
			return -1;
		}
		pfn = get_process_pfn(req.pid, req.addr);
		if (pfn < 0) {
			req.status = -1;
			req.pfn = 0;
		} else {
			req.status = 0;
			req.pfn = (uintptr_t)pfn;
		}
		if (copy_to_user((void __user *)arg, &req, sizeof(req)) != 0) {
			return -1;
		}
		break;
	}
	case OP_MODULE_BASE:
	{
		if (copy_from_user(&mb, (void __user *)arg, sizeof(mb)) != 0 || copy_from_user(name, (void __user *)mb.name, sizeof(name) - 1) != 0) {
			return -1;
		}
		mb.base = get_module_base(mb.pid, name, mb.index);
		if (copy_to_user((void __user *)arg, &mb, sizeof(mb)) != 0) {
			return -1;
		}
		break;
	}
	default:
		break;
	}
	return 0;
}


static int dispatch_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long pfn = vma->vm_pgoff;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (size != PAGE_SIZE) {
        return -EINVAL;
    }
    if (!pfn_valid(pfn)) {
        return -EINVAL;
    }
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        return -EAGAIN;
    }
    return 0;
}

static struct file_operations dispatch_functions = {
	.owner = THIS_MODULE,
	.open = dispatch_open,
	.release = dispatch_close,
	.unlocked_ioctl = dispatch_ioctl,
	.mmap = dispatch_mmap,
};

static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dispatch_functions,
};

int __init memkernel_entry(void)
{
	int ret;
	printk("[+] memkernel driver loaded (Made by @rajput_harshil)\n");
	ret = misc_register(&misc);
	return ret;
}

void __exit memkernel_unload(void)
{
	printk("[+] memkernel_unload (by @rajput_harshil)\n");
	misc_deregister(&misc);
}

module_init(memkernel_entry);
module_exit(memkernel_unload);

MODULE_DESCRIPTION("MemKernel / RT-Dev Driver for SM-A035F");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("@rajput_harshil");
