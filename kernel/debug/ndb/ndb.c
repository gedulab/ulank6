/*
    File Name: main.c
    Description: the driver interface for Nano Debug Beacon (NDB) module.
        NDB is a module of Nano Debugger. Please visit http://nanocode.cn
        for more information.
    Author: GEDU Shanghai Lab(GSL)
*/

#include "kgb.h"
#include "ndb.h"
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/timer.h> 
#include <linux/mm.h> 
#include <linux/uaccess.h>
#include <linux/kgdb.h>
#include <linux/version.h>
#include <asm/io.h>
#include <linux/delay.h>

#define NDB_VER_MAJOR 3
#define NDB_VER_MINOR 8

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)
#define HAVE_PROC_OPS
#endif

static struct proc_dir_entry* proc_ndb_entry = NULL;
extern ndb_gate_t g_ndb;
extern ndb_syspara_t ndb_syspara;
#define ARM64_HLT(imm) 

int ndb_breakin(int reason, long para1, long para2)
{
	register long ret asm ("x0");
    // asm("ldr x0, %0"::"r" (para1)); paras are already in registers
    // asm("ldr x1, %0"::"r" (para2));
    switch(reason) {
    case NDB_BRK_IMM_INIT://         0xE000 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_MOD_LOAD://     0xE001 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_MOD_UNLD://     0xE002 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_PROCESS_BORN:// 0xE003 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_PROCESS_EXIT:// 0xE004 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_THREAD_BORN://  0xE005 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_THREAD_EXIT://  0xE006
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
        break;
    case NDB_BRK_IMM_OOPS://         0xE007 
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));;
        break;
    default:
        asm("hlt %0" : : "I" (NDB_BRK_IMM_INIT));
    }

    return ret;
};

static int ndb_test_percpu(void)
{
    int i;
    int total_cpus;
    
    total_cpus = num_online_cpus();
    printk("Num online CPUS %d\n", total_cpus);
    for (i = 0; i < total_cpus; i++) {
        printk("percpu offset %px for cpu %d\n", (void*)__per_cpu_offset[i], i);
    }

    return 0;
}

static int ndb_test_current(void)
{
    struct task_struct* cur;
    
    cur = get_current();
    printk("current %px\n", cur);
    printk("\tmm %px\n", cur->mm);
    printk("\tmm offset %ld\n", offsetof(struct task_struct, mm));

    return 0;
}

static ssize_t proc_ndb_read(struct file* filp, char __user * buf, size_t count, loff_t * offp)
{
    int ret,n;
    const char * msg;
    
    printk(KERN_INFO "proc_ndb_read is called: file %p, buf %p count %zu offset %llx\n", filp, buf, count, *offp);

    msg = kgb_message();
    n = strlen(msg)+1;
    if (*offp < n) {
        ret = copy_to_user(buf, msg, n + 1);
        *offp = n + 1;
        ret = n + 1;
    }
    else {
        ret = 0;
    }

    return ret;
}

bool yl_touchpad_skip = false;

static void tp_timer_handle(struct timer_list* arg)
{
	yl_touchpad_skip = false;
	printk(KERN_INFO "touchpad enabled!\n");
}

static DEFINE_TIMER(tp_disable_timer, tp_timer_handle);

static void touchpad_disable(int sleep_time)
{
	yl_touchpad_skip = true;

	printk(KERN_INFO "touchpad disable countdown started, %ds!\n", sleep_time);
	mod_timer(&tp_disable_timer, jiffies + sleep_time * HZ);
}

static ssize_t proc_ndb_write(struct file* file, const char __user * buffer, size_t count, loff_t * data)
{
    char cmd[100] = { 0x00 }, *para;
    int i;
    unsigned long para_long;

    printk("proc_ndb_write called legnth 0x%lx, %p\n", count, buffer);
    if (count < 1) {
        printk("count <=1\n");
        
        return -EBADMSG; /* runt */
    }
    if (count > sizeof(cmd)) {
        printk("count > sizeof(cmd)\n");

        return -EFBIG;   /* too long */
    }
    if (copy_from_user(cmd, buffer, count)) {
        return -EFAULT;
    }

    para = strchr(cmd, ' ');
    if (para) {
        *para = 0;
        para++;
        para_long = simple_strtoul(para, NULL, 0);
    }

    if (strncmp(cmd, "lm", 2) == 0) {
        kgb_list_module();
    }
	else if (strncmp(cmd, "tp_l", 2) == 0) {
		touchpad_disable(10);
    }
	else if (strncmp(cmd, "tp_s", 3) == 0) {
		touchpad_disable(3);
    }
    else if (strncmp(cmd, "ps", 2) == 0) {
        kgb_list_process();
    }
    else if (strncmp(cmd, "percpu", 6) == 0) {
        ndb_test_percpu();
    }
    else if(strncmp(cmd,"current",7) == 0) {
        ndb_test_current();
    }
    else if(strncmp(cmd,"break", 5) == 0) {
        printk("triggering break\n");
        ndb_breakin(NDB_BRK_IMM_MANUAL, 0, 1);
        // arch_kgdb_breakpoint();
    }
    else if (strncmp(cmd, "iram", 4) == 0) {
        ndb_set_iram(NDB_NOTE_OFFSET, para_long);
    }
    else {
        for (i = 0; i < strlen(cmd); i++) {
            if (cmd[i] == '\n') {
                cmd[i] = '\0';

                break;
            }
        }

        printk("cmd = [%s]", cmd);
    }

    return count;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops proc_ndb_fops = {
    .proc_read = proc_ndb_read,
    .proc_write = proc_ndb_write,
};
#else
static const struct file_operations proc_ndb_fops = {
    .owner = THIS_MODULE,
    .read = proc_ndb_read,
    .write = proc_ndb_write,
};
#endif
bool __init ndb_need_init_break(void)
{
    uint64_t ndb_magic = read_sysreg_s(SYS_DBGWVRn_EL1(0));
  //  uint32_t *note = (uint32_t *)(long)(RK_IRAM_BASE + NDB_NOTE_OFFSET);
  // printk("ndb note reads %x\n", *note);                    GD /2024/05/22

    return ((ndb_magic) == 0xBBBBCCCC);  
}
#define YL_IRAM_BASE				0xff098000 // 0xff090000
#define YL_IRAM_SIZE				0x1000 // 0x9000
#define YL_NDB_NOTE_OFFSET			0x10
#define NDB_NOTE_KERNEL_BASE		0x88880000

#define YL1_SYSCON_GRF_BASE			0xfd58c000
#define YL1_BUS_IOC_BASE			0xfd5f8000
#define YL1_SYSCON_GRF_SIZE			0x1000
#define YL1_GRF_SOC_CON6			0x0318
#define BUS_IOC_GPIO4D_IOMUX_SEL_L	0x0098

#define GRF_HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

void __init ndb_check_breakin(void)
{
	int value;
	void __iomem* base;

	set_fixmap_io(FIX_EARLYCON_MEM_BASE, YL_IRAM_BASE & PAGE_MASK);
	base = (void __iomem *)__fix_to_virt(FIX_EARLYCON_MEM_BASE);
	base += YL_IRAM_BASE & ~PAGE_MASK;
	if(!base) {
		printk(KERN_ERR "ndb failed to map IRAM at 0x%x\n", YL_IRAM_BASE);
		return;
	}
	value = readl(base + 0x10);
	printk("ndb read YL_IRAM_BASE with value: 0x%x\n", value);
	if(value >= NDB_NOTE_KERNEL_BASE && value < NDB_NOTE_KERNEL_BASE + 0x10000) {
        // try breakin as first step
        if(ndb_breakin(NDB_BRK_IMM_BOOKED, (long)&g_ndb, (long)&ndb_syspara) != 0) { // if failed, waiting for debugger to initiate break
            printk("ndb is waiting for kernel debugger (%d)......\n", value - NDB_NOTE_KERNEL_BASE);
            mdelay(value - NDB_NOTE_KERNEL_BASE);
        }
	}

	return;
}
/*
init_level 0: early call, no fs yet
init_level 1: late call, fs ready
*/
int __init ndb_init(int init_level)
{
    int ret;

    printk("ndb %d.%d inits @L%d at %px\n", NDB_VER_MAJOR, NDB_VER_MINOR, init_level, ndb_init);
    yl_touchpad_skip = true;
	if(init_level == 0) {
        ret = kgb_init(init_level);
        if(ndb_need_init_break())
            ndb_breakin(NDB_BRK_IMM_INIT, (long)linux_banner, (long)&ndb_syspara);
    }
    else if (init_level == 1) {
        long pfn = sym_to_pfn(&ndb_syspara);
        g_ndb.syspara_ = kgb_map(pfn);
        ndb_syspara.kgb_base_ = (uint64_t)(long)g_ndb.syspara_;
        pr_notice("ndb mapped syspara(%lx) to %px\n", pfn, g_ndb.syspara_);
        ndb_set_iram(IRAM_NDB_SYSPARA, (uint64_t)g_ndb.syspara_);
    }
    else  {
        // update total CPUs, it may be changed
        ndb_syspara.total_cpu_ = num_possible_cpus(); 
        if(proc_ndb_entry == NULL) {
            /* Create /proc/ndb as interface for NdSrv and users */
            proc_ndb_entry = proc_create("ndb", 0, NULL, &proc_ndb_fops);
            if (proc_ndb_entry == NULL) {
                printk(KERN_ERR "ndb create proc entry failed\n");
            }
        }
        ret = kgb_init(init_level);
    }

    return ret;
}

static int __init ndb_module_init(void)
{
    return ndb_init(2);
}

static void __exit ndb_exit(void)
{
    kgb_exit();

    if (proc_ndb_entry) {
        proc_remove(proc_ndb_entry);
    }

    printk("ndb exits from 0x%p... Bye, GEDU friends\n", ndb_exit);

    return;
}

module_init(ndb_module_init);
module_exit(ndb_exit);
MODULE_AUTHOR("GEDU Shanghai Lab(GSL)");
MODULE_DESCRIPTION("The Beacon LKM for Nano Debugger");
MODULE_LICENSE("GPL");
