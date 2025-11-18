/******************************************************************************/
/* Important Fall 2025 CSCI 402 usage information:                            */
/*                                                                            */
/* This fils is part of CSCI 402 kernel programming assignments at USC.       */
/*         53616c7465645f5fd1e93dbf35cbffa3aef28f8c01d8cf2ffc51ef62b26a       */
/*         f9bda5a68e5ed8c972b17bab0f42e24b19daa7bd408305b1f7bd6c7208c1       */
/*         0e36230e913039b3046dd5fd0ba706a624d33dbaa4d6aab02c82fe09f561       */
/*         01b0fd977b0051f0b0ce0c69f7db857b1b5e007be2db6d42894bf93de848       */
/*         806d9152bd5715e9                                                   */
/* Please understand that you are NOT permitted to distribute or publically   */
/*         display a copy of this file (or ANY PART of it) for any reason.    */
/* If anyone (including your prospective employer) asks you to post the code, */
/*         you must inform them that you do NOT have permissions to do so.    */
/* You are also NOT permitted to remove or alter this comment block.          */
/* If this comment block is removed or altered in a submitted file, 20 points */
/*         will be deducted.                                                  */
/******************************************************************************/

#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadowd.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"
#include "proc/kmutex.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/disk/ata.h"
#include "drivers/tty/virtterm.h"
#include "drivers/pci.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"

#include "test/kshell/kshell.h"
#include "test/s5fs_test.h"

#ifndef TTY_MAJOR
#define TTY_MAJOR 2
#endif

int kprintf(const char *fmt, ...);
GDB_DEFINE_HOOK(initialized)

void      *bootstrap(int arg1, void *arg2);
void      *idleproc_run(int arg1, void *arg2);
kthread_t *initproc_create(void);
void      *initproc_run(int arg1, void *arg2);
void      *final_shutdown(void);
void      gdb_init(void);

extern void proc_register_kshell(struct kshell *ksh);
extern void kmutex_register_kshell(struct kshell *ksh);
extern void faber_thread_test(void);
extern void sunghan_test(void);
extern void sunghan_deadlock_test(void);
extern void *vfstest_main(int, void *);
extern int faber_fs_thread_test(struct kshell *ksh, int argc, char **argv);
extern int faber_directory_test(struct kshell *ksh, int argc, char **argv);

/* Kshell command functions */
static int my_sunghan_test(kshell_t *ksh, int argc, char **argv);
static int my_deadlock_test(kshell_t *ksh, int argc, char **argv);
static int kshell_cmd_vfstest(kshell_t *ksh, int argc, char **argv);
static int   kshell_cmd_faber(kshell_t *ksh, int argc, char **argv);
/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active( * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
bootstrap(int arg1, void *arg2)
{
        /* If the next line is removed/altered in your submission, 20 points will be deducted. */
        dbgq(DBG_TEST, "SIGNATURE: 53616c7465645f5f033d07da66a59f8d7436cac31b93b055ba62577f13a46c89b09a22fa146e50783208aaf4ee6b398e\n");
        /* necessary to finalize page table information */
        pt_template_init();


    proc_t *idle = proc_create("idle");
    KASSERT(idle);


    kthread_t *idlethr = kthread_create(idle, idleproc_run, 0, NULL);
    KASSERT(idlethr);


    curproc = idle;
    curthr  = idlethr;

    context_make_active(&idlethr->kt_ctx);

        panic("weenix returned to bootstrap()!!! BAD!!!\n");
	return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
idleproc_run(int arg1, void *arg2)
{
        int status;
        pid_t child;

        /* create init proc */
        kthread_t *initthr = initproc_create();

        init_call_all();
        GDB_CALL_HOOK(initialized);

        /* Create other kernel threads (in order) */

#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */
vnode_t *rt = vfs_root_vn;
KASSERT(rt);
curproc->p_cwd = rt;      vref(rt);
initthr->kt_proc->p_cwd = rt; vref(rt);
        /* Here you need to make the null, zero, and tty devices using mknod */
        /* You can't do this until you have VFS, check the include/drivers/dev.h
         * file for macros with the device ID's you will need to pass to mknod */
do_mkdir("/dev");
do_mknod("/dev/null", S_IFCHR, MKDEVID(1,0));
do_mknod("/dev/zero", S_IFCHR, MKDEVID(1,1));
do_mknod("/dev/tty0", S_IFCHR, MKDEVID(TTY_MAJOR,0));
do_mknod("/dev/tty1", S_IFCHR, MKDEVID(TTY_MAJOR,1));
do_mknod("/dev/sda",  S_IFBLK, MKDEVID(1,0));
dbg(DBG_PRINT, "(GRADING2B)\n");
#endif


        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
       intr_enable();

        /* Run initproc */
        sched_make_runnable(initthr);
        /* Now wait for it */
        child = do_waitpid(-1, 0, &status);
        KASSERT(PID_INIT == child);

#ifdef __VFS__
if (curproc->p_cwd) { vput(curproc->p_cwd); curproc->p_cwd = NULL; }
#endif

        return final_shutdown();
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */
kthread_t *
initproc_create(void)
{
    proc_t *init = proc_create("init");
    KASSERT(NULL != init);    
    KASSERT(PID_INIT == init->p_pid);

    kthread_t *initthr = kthread_create(init, initproc_run, 0, NULL);
    KASSERT(NULL != initthr);

    return initthr;
}


/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/sbin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */


#ifdef __DRIVERS__
static void *faber_entry(int arg1, void *arg2)
{
    (void)arg1; (void)arg2;
    faber_thread_test();
    do_exit(0);            
    return NULL;
}

static int kshell_cmd_faber(kshell_t *ksh, int argc, char **argv)
{
    (void)ksh; (void)argc; (void)argv;

    proc_t    *p   = proc_create("faber_test");
    KASSERT(p != NULL);
    kthread_t *thr = kthread_create(p, faber_entry, 0, NULL);
    KASSERT(thr != NULL);

    sched_make_runnable(thr);

    int status = 0;
    do_waitpid(p->p_pid, 0, &status);

    return 0;
}


static int my_sunghan_test(kshell_t *ksh, int argc, char **argv)
{
    proc_t *proc = proc_create("sunghan_test");
    kthread_t *thr = kthread_create(proc, (kthread_func_t)sunghan_test, 0, NULL);
    
    sched_make_runnable(thr);
    
    int status;
    do_waitpid(proc->p_pid, 0, &status);
    
    return 0;
}

static int my_deadlock_test(kshell_t *ksh, int argc, char **argv)
{
    proc_t *proc = proc_create("deadlock_test");
    kthread_t *thr = kthread_create(proc, (kthread_func_t)sunghan_deadlock_test, 0, NULL);
    
    sched_make_runnable(thr);
    
    int status;
    do_waitpid(proc->p_pid, 0, &status);
    
    return 0;
}

static int kshell_cmd_vfstest(kshell_t *ksh, int argc, char **argv)
{
        proc_t *proc = proc_create("vfs_test");
        KASSERT(proc != NULL);

        kthread_t *thr = kthread_create(proc, (kthread_func_t)&vfstest_main, 1, NULL);
        KASSERT(thr != NULL);

        sched_make_runnable(thr);
        int status;
        do_waitpid(proc->p_pid, 0, &status);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
}

static void * Shelltest(int arg1, void *arg2){

        kshell_add_command("vfstest", (kshell_cmd_func_t)&kshell_cmd_vfstest, "Run vfs_test().");
        kshell_add_command("thrtest", (kshell_cmd_func_t)&faber_fs_thread_test, "Run faber_fs_thread_test().");
        kshell_add_command("dirtest", (kshell_cmd_func_t)&faber_directory_test, "Run faber_directory_test().");

        kshell_t *kshell = kshell_create(0);
        if (NULL == kshell) {
            panic("init: Couldn't create kernel shell\n");
        }

        while (kshell_execute_next(kshell) > 0);
        kshell_destroy(kshell);
/*
do_close(0);
do_close(1);
do_close(2);
*/
        return NULL;
}
#endif

void *
initproc_run(int arg1, void *arg2)
{
    int st;

/*
if (curproc->p_cwd) vput(curproc->p_cwd);
vref(vfs_root_vn);
curproc->p_cwd = vfs_root_vn;
dbg(DBG_PRINT, "cwd set to '/'\n");

int rc;
rc = do_mkdir("/dev"); dbg(DBG_PRINT, "mkdir /dev -> %d\n", rc);
rc = do_mknod("/dev/null", S_IFCHR, MKDEVID(1,0)); dbg(DBG_PRINT, "mknod null -> %d\n", rc);
rc = do_mknod("/dev/zero", S_IFCHR, MKDEVID(1,1)); dbg(DBG_PRINT, "mknod zero -> %d\n", rc);
rc = do_mknod("/dev/tty0", S_IFCHR, MKDEVID(TTY_MAJOR,0)); dbg(DBG_PRINT, "mknod tty0 -> %d\n", rc);
rc = do_mknod("/dev/tty1", S_IFCHR, MKDEVID(TTY_MAJOR,1)); dbg(DBG_PRINT, "mknod tty1 -> %d\n", rc);

int fd = do_open("/dev/tty0", O_RDWR);
dbg(DBG_PRINT, "do_open tty0 -> %d\n", fd);
if (fd < 0) { dbg(DBG_PRINT, "open fail = %d\n", fd); }
*/


#ifdef __DRIVERS__
//Shelltest(0, NULL);
    char *argv[] = { "/sbin/init", NULL };
    char *envp[] = { NULL };
 kernel_execve("/sbin/init", argv, envp);
#endif

while (do_waitpid(-1, 0, NULL) != -ECHILD) {}
    return NULL;
}

