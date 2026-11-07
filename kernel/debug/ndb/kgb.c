/*
    File Name: kgd.c
    Descroption: The header file for KGB(Kernel Geography Block).
        KGB is a memory page to present the target landmarks to
        the host side of Nano Debugger.
    Author: GEDU Shanghai Lab(GSL)

    rk3588 platform -> oops when loading drivers:
        KFN_PTR
        log_buf_get()
        log_len_get()
*/

#include "kgb.h"
#include "dbgrdata.h"
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <asm/sysreg.h>

#define NDB_IN_KERNEL

#ifndef NDB_IN_KERNEL
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
    #define KPROBE_LOOKUP 1
    #include <linux/kprobes.h>
    static struct kprobe kp = {
        .symbol_name = "kallsyms_lookup_name"
    };
    typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
    kallsyms_lookup_name_t kallsyms_lookup_name_ptr;
    #define KFN_PTR(s) kallsyms_lookup_name_ptr(#s) // to avoid the annoying name conflict
    #else
    #include <linux/kallsyms.h>
    #define KFN_PTR(s) kallsyms_lookup_name(#s)
    #endif
#else
    #define KFN_PTR(s)  s 
    ndb_syspara_t ndb_syspara __page_aligned_data;
#endif
extern uint64_t __vvar_page;      //GD 
extern uint64_t current_task;     //GD
extern uint64_t __entry_task;      //GD
extern uint64_t total_forks;
extern uint64_t nr_threads;

extern struct cpuinfo_x86 boot_cpu_data;
extern struct task_struct init_task;
extern uint64_t phys_base;
struct module module_kernel;
extern struct list_head modules;

const uint32_t NDP_MARKER_A = 0x20180821;
const uint32_t NDP_MARKER_B = 0x47454455; // GEDU
typedef char* (*log_buf_addr_get_t)(void);
typedef u32(*log_buf_len_get_t)(void);
struct list_head g_dbgrdatalist = { NULL, NULL };
dbgrdata64_t g_dbgrdataentry;   // entry for Nano Debugger

ndb_gate_t g_ndb={0};
// extern struct list_head *kdb_modules;
int ndb_set_iram(int offset, uint64_t para)
{
    void __iomem* base;
   // void* base;     //GD 2024/05/15 
    //int reads;  //GD
#ifdef IRAM_USE_IOREMAP // not available at early boot
    base = ioremap(RK_IRAM_BASE + 0x8000, 0x1000);
    if (base == NULL) {
        printk(KERN_ERR "NDB failed to map IRAM at %x\n", RK_IRAM_BASE);
        
        return -1;
    }

    reads = readl(base + NDB_NOTE_OFFSET);
    printk(KERN_ERR "GDK8_NDB_NOTE %x\n", reads);
    if (para >= 0x888) {
        writel(para + 0x88880000, base + NDB_NOTE_OFFSET);
    }

    reads = readl(base + NDB_NOTE_OFFSET);
    printk(KERN_ERR "GDK8_NDB_NOTE Current Value: %x\n", reads);

    iounmap(base);
#else
	//int value;   //GD
	//void __iomem* base;    //GD

	set_fixmap_io(FIX_NDB_IRAM, RK_IRAM_BASE & PAGE_MASK);
	base = (void __iomem *)__fix_to_virt(FIX_NDB_IRAM);
	base += RK_IRAM_BASE & ~PAGE_MASK;
	if(!base) {
		return -1;
	}
	writeq(para, base + offset);
    pr_notice("NDB sets IRAM+%d to %llx reads %llx\n", offset, para, readq(base + offset));
#endif    
    return 0;
}

static int kgb_init_dbgrdata(void)
{
    INIT_LIST_HEAD(&g_dbgrdatalist);
    INIT_LIST_HEAD(&g_dbgrdataentry.head.list);
    g_dbgrdataentry.Revision = 1;

    list_add_tail(&g_dbgrdataentry.head.list, &g_dbgrdatalist);

    return 0;
}

static void* kgb_orig_modules_head(long module_addr)
{
    struct module* mod;
    struct  list_head* next, * list_head_orig;
    typedef struct module *(*__module_address_t)(unsigned long addr);
    __module_address_t pfn = (__module_address_t)KFN_PTR(__module_address);

	printk("ndb get moudle 0x%lx\n", module_addr);

	if(THIS_MODULE == NULL) { // build into kernel
		printk("ndb is built into kernel\n");
        return &modules; //kdb_modules;
    }

    mod = pfn(module_addr);
    list_head_orig = mod->list.prev;
    next = list_head_orig->next;

    printk("ndb module list head (original) = %px\n", list_head_orig);

    return (void*)list_head_orig;

}

static unsigned long kgb_kernel_base(void)
{
    return (unsigned long)KFN_PTR(
#ifdef CONFIG_X86
    _text
#else
    _stext
#endif
    );        //GD (unsigned long) 2024/05/15
}

static unsigned long kgb_kernel_length(unsigned long base)
{
    //long end;
    unsigned long end;     //GD (unsigned long) 2024/05/15

    end = (unsigned long)KFN_PTR(__bss_stop);                  //GD   (unsigned long)  2024/05/15
    if(end == 0) {
        end = (unsigned long)KFN_PTR(_etext); // for arm64, no bss_stop, try _etext           //GD (unsigned long)  2024/05/15
    }

    return (end - base);
}

static long kgb_module_head(long* head_orig)
{
    struct  list_head * head;

    module_kernel.state = 3;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    module_kernel.mem[MOD_TEXT].base = (void *)kgb_kernel_base();
    module_kernel.mem[MOD_TEXT].size = kgb_kernel_length(kgb_kernel_base());
#else
    module_kernel.core_layout.base = (void *)kgb_kernel_base();
    module_kernel.core_layout.size = kgb_kernel_length(kgb_kernel_base());
#endif
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
    strscpy(module_kernel.name, "vmlinux", 8);
#else
    strlcpy(module_kernel.name, "vmlinux", 8);
#endif

    INIT_LIST_HEAD(&module_kernel.list);
    INIT_LIST_HEAD(&g_ndb.full_module_list_head_);
    head = (struct  list_head*)head_orig;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    printk("ndb: original module head = %px kernel %px size %x\n", head,
        module_kernel.mem[MOD_TEXT].base, module_kernel.mem[MOD_TEXT].size);
#else
    printk("ndb: original module head = %px kernel %px size %x\n", head,
        module_kernel.core_layout.base, module_kernel.core_layout.size);
#endif
    g_ndb.full_module_list_head_.next = &module_kernel.list;
    module_kernel.list.prev = &g_ndb.full_module_list_head_;
    module_kernel.list.next = head;

    return (long)&g_ndb.full_module_list_head_;
}

static int kgb_init_syspara(ptr_ndb_syspara_t psyspara, long module_addr)
{
    long total_cpu;
    log_buf_addr_get_t log_buf_get;
    log_buf_len_get_t  log_len_get;
    ndb_os_struct_sym* sym;

    sym = &(psyspara->kernelstructs_);
    g_ndb.orig_module_list_head_ = (struct list_head*)kgb_orig_modules_head(module_addr);
    log_buf_get = (log_buf_addr_get_t)KFN_PTR(log_buf_addr_get);
    log_len_get = (log_buf_len_get_t)KFN_PTR(log_buf_len_get);
    // for_each_online_cpu(total_cpu);
    total_cpu = num_possible_cpus(); // num_online_cpus();
    psyspara->ndp_markera_ = NDP_MARKER_A;
    psyspara->ndp_markerb_ = NDP_MARKER_B;
    psyspara->kernel_base_ = kgb_kernel_base();
    psyspara->kernel_length_ = kgb_kernel_length(psyspara->kernel_base_);
    psyspara->kgb_base_ = (uint64_t)psyspara;
    psyspara->module_list_head_orig_ = (uint64_t)g_ndb.orig_module_list_head_;
    psyspara->module_list_head_ = kgb_module_head((long*)g_ndb.orig_module_list_head_);

#ifdef CONFIG_X86
    psyspara->machine_type_ = boot_cpu_data.x86;
#else
    psyspara->machine_type_ = 0xaa64;
#endif
    psyspara->platform = NDB_PLATFORM_LINUX;
    psyspara->process_list_head_ = (uint64_t)&init_task.tasks;
    psyspara->total_cpu_ = total_cpu;
    psyspara->printk_buffer_addr_ = (uint64_t)log_buf_get();
    psyspara->printk_buffer_length_ = (uint32_t)log_len_get();
    psyspara->percpu_offset_ = (uint64_t)__per_cpu_offset;
    psyspara->debugger_data_list_head_ = (uint64_t)&g_dbgrdatalist;
    psyspara->mailbox_base_ = 0; // mailboxe are not used on yourland, add by macallan 20230314
 //   psyspara->phys_base_ = KFN_PTR(phys_base);         GD  暂时注释  2024/05/15
    psyspara->init_uts_ns_ = (uint64_t)&init_uts_ns;
 //   psyspara->vvar_page_ = KFN_PTR(__vvar_page);     GD  暂时注释  2024/05/15    
#ifdef CONFIG_X86
#else
    psyspara->entry_task_ = KFN_PTR(__entry_task); 
    psyspara->ver_major = (uint8_t)(LINUX_VERSION_CODE>>16); // LINUX_VERSION_MAJOR;//5;
    psyspara->ver_minor = (uint16_t)((LINUX_VERSION_CODE>>8)&&0xFF);//10;
    psyspara->ver_revison = (uint16_t)(LINUX_VERSION_CODE && 0xFF);//LINUX_VERSION_SUBLEVEL;//110;
    psyspara->ver_distributor = 0;
    psyspara->uptime_ = ktime_get_mono_fast_ns();
    psyspara->total_forks_ = KFN_PTR(total_forks);             psyspara->total_forks_ = KFN_PTR(total_forks);
    psyspara->nr_threads_ = KFN_PTR(nr_threads);                
#endif
    sym->vma_size_ = sizeof(struct vm_area_struct);
    sym->vma_file_ = offsetof(struct vm_area_struct, vm_file);
    sym->module_size_ = sizeof(struct module);
    sym->module_name_ = offsetof(struct module, name);
    sym->module_version_ = offsetof(struct module, version);
    sym->module_srcversion_ = offsetof(struct module, srcversion);
    sym->module_syms_ = offsetof(struct module, syms);
    sym->module_crcs_ = offsetof(struct module, crcs);
    sym->module_num_syms_ = offsetof(struct module, num_syms);
    sym->module_init_ = offsetof(struct module, init);
    sym->module_list_ = offsetof(struct module, list);
    sym->module_args_ = offsetof(struct module, args);
    sym->module_exit_ = offsetof(struct module, exit);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    /* 6.4 及以后：使用 mem 数组 */
    sym->module_core_ = offsetof(struct module, mem[MOD_TEXT]);
    sym->module_core_size_ = offsetof(struct module, mem[MOD_TEXT]) + offsetof(struct module_memory, size);
    sym->module_core_text_size_ = offsetof(struct module, mem[MOD_TEXT]) + offsetof(struct module_memory, size);
    sym->module_init_size_ = offsetof(struct module, mem[MOD_INIT_TEXT]) + offsetof(struct module_memory, size);
    sym->module_init_text_size_ = offsetof(struct module, mem[MOD_INIT_TEXT]) + offsetof(struct module_memory, size);
#else
    /* 6.4 之前的旧代码 */
    sym->module_core_ = offsetof(struct module, core_layout);
    sym->module_core_size_ = offsetof(struct module, core_layout) + offsetof(struct module_layout, size);
    sym->module_core_text_size_ = offsetof(struct module, core_layout) + offsetof(struct module_layout, text_size);
    sym->module_init_size_ = offsetof(struct module, init_layout) + offsetof(struct module_layout, size);
    sym->module_init_text_size_ = offsetof(struct module, init_layout) + offsetof(struct module_layout, text_size);
#endif
    sym->module_sect_attrs_ = offsetof(struct module, sect_attrs);
    sym->module_attribute_size_ = sizeof(struct module_attribute);
    sym->attribute_group_size_ = sizeof(struct attribute_group);
  //  sym->pcpu_current_task_ = (uint32_t)KFN_PTR(current_task);     GD 暂时注释  2024/05/15
    sym->task_size = sizeof(struct task_struct);
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
    sym->task_state = offsetof(struct task_struct, __state);
#else
    sym->task_state = offsetof(struct task_struct, state);
#endif
    sym->task_stack = offsetof(struct task_struct, stack);
    sym->task_usage = offsetof(struct task_struct, usage);
    sym->task_flags = offsetof(struct task_struct, flags);
    sym->task_tasks = offsetof(struct task_struct, tasks);
    sym->task_pid  = offsetof(struct task_struct, pid);
    sym->task_comm = offsetof(struct task_struct, comm);
    sym->task_mm = offsetof(struct task_struct, mm);
    sym->mm_size = sizeof(struct mm_struct);
    sym->task_real_parent = offsetof(struct task_struct, real_parent);
    sym->task_start_time = offsetof(struct task_struct, start_time);
    sym->task_fs = offsetof(struct task_struct, fs);
    sym->task_files = offsetof(struct task_struct, files);
    sym->task_thread = offsetof(struct task_struct, thread);
    sym->mm_pgd = offsetof(struct mm_struct, pgd);
    sym->ns_name = offsetof(struct uts_namespace, name);
    sym->utsname_size = sizeof(struct new_utsname);
    sym->file_path = offsetof(struct file, f_path);
    sym->path_dentry = offsetof(struct path, dentry);
    sym->dentry_d_name = offsetof(struct dentry, d_name);
    sym->dentry_d_iname = offsetof(struct dentry, d_iname);
    sym->task_active_mm = offsetof(struct task_struct, active_mm);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    /* 6.1+ 已移除 mm_struct.mmap，此处偏移量不再有效 */
    sym->mm_mmap = 0;
#else
    sym->mm_mmap = offsetof(struct mm_struct, mmap);
#endif
    sym->dentry_size = sizeof(struct dentry);
    sym->qstr_size = sizeof(struct qstr);
    sym->qstr_name = offsetof(struct qstr, name);
    sym->vma_vm_flags = offsetof(struct vm_area_struct, vm_flags);

    printk("ndb syspara is inited. kernel base:%px length:%x module head:%px task head:%px\n",
        (void*)psyspara->kernel_base_, psyspara->kernel_length_, 
        (void*)g_ndb.orig_module_list_head_, (void*)psyspara->process_list_head_);

    return 0;
}
/*
geduer@ulan:~$ dmesg | grep ndb
[   22.056647] ndb 3.0 inits @L0 at ffffffc012901df4
[   22.056683] ndb_syspara (kgb) is mappped to ffffffc013255000 (pfn=0)
[   22.056726] ndb get moudle 0xffffffc0101b6330
[   22.056742] ndb is built into kernel
[   23.136849] ndb 3.0 inits @L1 at ffffffc012901df4
[   23.136878] ndb mapped pfn e1055 to fffffffefe5fe000 VMALLOC_START=ffffffc010000000 VMALLOC_END=fffffffebfff0000
[   23.136907] ndb mapped syspara(e1055) to fffffffefe5fe000
[   25.412308] ndb 3.0 inits @L2 at ffffffc012901df4
[   89.570262] proc_ndb_read is called: file 0000000030890e23, buf 00000000a5ce4de4 count 131072 offset 0
[   89.570546] proc_ndb_read is called: file 0000000030890e23, buf 00000000a5ce4de4 count 131072 offset 1c8
*/
// #ifndef NDB_IN_KERNEL      // GD  2024/05/15
void* kgb_map(long pfn)
{
    void __iomem* base;
   // struct page* page;

#ifdef CONFIG_X86
    set_fixmap_io(FIX_DBGP_BASE, pfn << PAGE_SHIFT);
    base = (void __iomem*)__fix_to_virt(FIX_DBGP_BASE);
#else
    set_fixmap(FIX_NDB_SYS_PARA, pfn << PAGE_SHIFT);
    base = (void __iomem*)__fix_to_virt(FIX_NDB_SYS_PARA);
    //page = pfn_to_page(pfn);
    //base = kmap(page); // kmap_atomic(page); both worked and result is similiar
#endif

    if (base == NULL) {
        printk("failed to map pfn %lx for KGB\n", pfn);  
    
        return NULL;
    }

    printk("ndb mapped pfn %lx to %px VMALLOC_START=%lx VMALLOC_END=%lx\n", pfn, base, VMALLOC_START, VMALLOC_END);
    
    return base;
}
// #endif // NDB_IN_KERNEL   GD

static int kgb_plant(void)
{
    long pfn = 0;

#ifndef  NDB_IN_KERNEL
    g_ndb.pages_ = alloc_pages_node(numa_node_id(), __GFP_HIGHMEM | __GFP_ZERO, 0);
    if (!g_ndb.pages_) {
        printk("alloc page for KGB failed\n");
    
        return -ENOMEM;
    }

    pfn = page_to_pfn(g_ndb.pages_);
    g_ndb.syspara_ = kgb_map(pfn);
    if (!g_ndb.syspara_) {
        printk("ndb_map_memory is error\n");
    
        return -1;
    }    
#else
    g_ndb.syspara_ = &ndb_syspara;
    
    write_sysreg_s(g_ndb.syspara_, SYS_DBGWVRn_EL1(0)); // SYS_TPIDR_EL1) // caused linux hang on boot
#endif

    printk("ndb_syspara (kgb) is at %px (pfn=%lx)\n", g_ndb.syspara_, pfn);   

    return 0;
}

void kgb_list_process(void)
{
    struct list_head* ptr_list_next;
    struct task_struct* temp_task;

    ptr_list_next = init_task.tasks.next;
    while (ptr_list_next != &init_task.tasks) {
        temp_task = list_entry(ptr_list_next, struct task_struct, tasks);
        printk("pid = %d, process name = %s\n", temp_task->pid, temp_task->comm);

        ptr_list_next = ptr_list_next->next;
    }

    return;
}

void kgb_list_module(void)
{
    struct  module* mod;
    struct  list_head* next;
    struct  list_head* head;
    struct list_head* orig_head;
    
    head = (struct  list_head*)g_ndb.syspara_->module_list_head_;
    orig_head = (struct  list_head*)g_ndb.syspara_->module_list_head_orig_;

    next = head->next;  
    printk("enumerating ndb's module list from head %px (next %px)\n", &g_ndb.full_module_list_head_, next);
    while (next != orig_head) {
        mod = list_entry(next, struct module, list);
        printk("module %s @ %px\n", mod->name, next);

        next = next->next;
    }

    return;
}

const char* kgb_message(void)
{
    ptr_ndb_syspara_t syspara;
    ptr_ndb_os_struct_sym ptr_sym;
    char * lpsz;

    if(g_ndb.msg_==NULL) {
        g_ndb.msg_ = vmalloc(KGB_MSG_MAX);
    }

    syspara = (ptr_ndb_syspara_t)g_ndb.syspara_;
    ptr_sym = &syspara->kernelstructs_;

    lpsz = g_ndb.msg_;
    sprintf(lpsz, "marker=%x.%x, kgb=%px, kernel=%px\n", syspara->ndp_markera_, syspara->ndp_markerb_,
        (void*)syspara->kgb_base_,(void*)syspara->kernel_base_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module list=%px, orig=%px, process list=%px\n", (void*)syspara->module_list_head_,
        (void*)syspara->module_list_head_orig_,(void*)syspara->process_list_head_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "vma_size=%d, vma_file=%d\n", ptr_sym->vma_size_, ptr_sym->vma_file_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "srcversion=%d, syms=%d\n", ptr_sym->module_srcversion_, ptr_sym->module_syms_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "crcs=%d, num_syms=%d\n", ptr_sym->module_crcs_, ptr_sym->module_num_syms_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module_init=%d, module_list=%d\n", ptr_sym->module_init_, ptr_sym->module_list_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "args=%d, exit=%d\n", ptr_sym->module_args_, ptr_sym->module_exit_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module_core=%d, core_size=%d\n", ptr_sym->module_core_, ptr_sym->module_core_size_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "core_text_size=%d, init_size=%d\n", ptr_sym->module_core_text_size_, ptr_sym->module_init_size_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "printk_buffer_addr=%llx, printk_buffer_length=%d\n", 
        syspara->printk_buffer_addr_, syspara->printk_buffer_length_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "total_cpu=%d, machine_type=%x, platform=%d\n", syspara->total_cpu_, syspara->machine_type_, (int)syspara->platform);

    return g_ndb.msg_;
}

static int kgb_on_module_load(struct notifier_block *self, unsigned long val, void * data)
{
    //update the fake module head
    module_kernel.list.next = g_ndb.orig_module_list_head_->next;
#ifdef NDB_DBG_MOD_LOAD // too noisy, disable it
    printk("ndb updated %p on self %p val %lx, data %p\n", g_ndb.orig_module_list_head_, self, val, data);
#endif 
    return 0;
}

static struct notifier_block kgb_module_load_nb = {
    .notifier_call = kgb_on_module_load,
};

int kgb_init(int level)
{
    int ret;

#ifdef KPROBE_LOOKUP
    register_kprobe(&kp);
    kallsyms_lookup_name_ptr = (kallsyms_lookup_name_t) kp.addr;
    unregister_kprobe(&kp);
#endif
    if(level ==0 ){
        ret = kgb_plant();
        if(ret != 0 ) {
            printk("Plant of KGB failed with %x\n", ret);

            return ret;
        }
        kgb_init_dbgrdata();
        printk("kgb_init addr %pS\n", kgb_init);
        ret = kgb_init_syspara(g_ndb.syspara_, (long)kgb_init);
        // kgdb_breakpoint();
    }
    else {
        register_module_notifier(&kgb_module_load_nb);
    }

    return ret;
}

int kgb_exit(void)
{
    unregister_module_notifier(&kgb_module_load_nb);

    if(g_ndb.pages_)
        __free_pages(g_ndb.pages_, 0);

    if(g_ndb.msg_!=NULL)
        vfree(g_ndb.msg_);

    return 0;
}
