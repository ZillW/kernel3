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

#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

#include "test/kshell/kshell.h"

extern void faber_thread_test(void);

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
pid_t new_pid = _proc_getid();
        KASSERT(new_pid != -1);

        proc_t* new_proc = (proc_t *)slab_obj_alloc(proc_allocator);
        KASSERT(NULL != new_proc);

        new_proc->p_pagedir = pt_create_pagedir();
        new_proc->p_pid = new_pid;
        new_proc->p_state = PROC_RUNNING;

        new_proc->p_vmmap = vmmap_create();
        if (new_proc->p_vmmap == NULL) {
                pt_destroy_pagedir(new_proc->p_pagedir);
                slab_obj_free(proc_allocator, new_proc);
                return NULL;
        }
        new_proc->p_vmmap->vmm_proc = new_proc;

        strncpy(new_proc->p_comm, name, PROC_NAME_LEN);
        new_proc->p_comm[PROC_NAME_LEN - 1] = '\0';

        sched_queue_init(&new_proc->p_wait);
        list_init(&new_proc->p_threads);
        list_init(&new_proc->p_children);

        new_proc->p_pproc = curproc;
        if (NULL != curproc) {
                list_insert_tail(&(curproc->p_children), &(new_proc->p_child_link));
        }

#ifdef __VFS__
        if (vfs_root_vn != NULL)
        {
                new_proc->p_cwd = vfs_root_vn;
                vref(vfs_root_vn);
		dbg(DBG_PRINT, "(GRADING2B)\n");
        }

        int i;
        for (i = 0; i < NFILES; i++) {
                new_proc->p_files[i] = NULL;
		//dbg(DBG_PRINT, "(GRADING2B)\n");
        }
	dbg(DBG_PRINT, "(GRADING2B)\n");
#endif

        KASSERT(PID_IDLE != new_pid || list_empty(&_proc_list));
        KASSERT(PID_INIT != new_pid || PID_IDLE == curproc->p_pid);

        if (PID_INIT == new_pid) {
                proc_initproc = new_proc;
        }

        KASSERT(NULL != new_proc->p_pagedir);
        list_insert_tail(proc_list(), &(new_proc->p_list_link));

        dbg(DBG_PRINT, "(GRADING2B)\n");
        return new_proc;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
        proc_t *p = curproc;

    KASSERT(NULL != proc_initproc);
    KASSERT(1 <= curproc->p_pid); 
    KASSERT(NULL != curproc->p_pproc);

        p->p_status = status;

#ifdef __VFS__
/*if (p->p_cwd) {
        vput(p->p_cwd);
        p->p_cwd = NULL;
}*/
#endif


        if (proc_initproc && p != proc_initproc) {
                proc_t *c;
                list_iterate_begin(&p->p_children, c, proc_t, p_child_link) {
                        list_remove(&c->p_child_link);
                        c->p_pproc = proc_initproc;
                        list_insert_tail(&proc_initproc->p_children, &c->p_child_link);
                } list_iterate_end();
        }

    p->p_state = PROC_DEAD;
        if (p->p_pproc) {
                sched_wakeup_on(&p->p_pproc->p_wait);
        }

KASSERT(NULL != curproc->p_pproc);
KASSERT(curthr->kt_proc == curproc);
    
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
    KASSERT(p != NULL);
    if (p == curproc) {
        (void)do_exit(status);
        return;
    }
kthread_cancel(list_head(&p->p_threads, kthread_t, kt_plink), (void *)status);
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
    proc_t *p;

    list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
        if (p == curproc)                      { continue; }
        if (p->p_pproc == NULL)                { continue; }
        if (p->p_pproc->p_pid == PID_IDLE)     { continue; }
        proc_kill(p, 0);
    } list_iterate_end();

    if (curproc->p_pproc && curproc->p_pproc->p_pid != PID_IDLE) {
        proc_kill(curproc, 0);

    }
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
        proc_cleanup((int)(intptr_t)retval);
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
while (1)
        {
                proc_t *child_proc = NULL;
                int matching_child_exists = 0;

                list_iterate_begin(&curproc->p_children, child_proc, proc_t, p_child_link) {
                        
                        if (pid == -1 || child_proc->p_pid == pid) {
                                matching_child_exists = 1;

                                if (child_proc->p_state == PROC_DEAD) {
                                        kthread_t *zombie_thread = list_head(&(child_proc->p_threads), kthread_t, kt_plink);
                                        
                                        KASSERT(NULL != child_proc);
                                        KASSERT(-1 == pid || child_proc->p_pid == pid);
                                        KASSERT(KT_EXITED == zombie_thread->kt_state);
                                        KASSERT(NULL != child_proc->p_pagedir);
                                        
                                        *status = child_proc->p_status;
                                        pid_t reaped_pid = child_proc->p_pid;

#ifdef __VFS__
                                        if (child_proc->p_cwd) {
                                                vput(child_proc->p_cwd);
                                                child_proc->p_cwd = NULL;
                                        }
                                        int file_idx;
                                        for (file_idx = 0; file_idx < NFILES; file_idx++) {
                                                if (child_proc->p_files[file_idx]) {
                                                        fput(child_proc->p_files[file_idx]);
                                                        dbg(DBG_PRINT, "(GRADING2B)\n");
                                                }
                                                dbg(DBG_PRINT, "(GRADING2B)\n");
                                                child_proc->p_files[file_idx] = NULL;
                                        }
                                        dbg(DBG_PRINT, "(GRADING2B)\n");
#endif
                                        kthread_destroy(zombie_thread);

                                        if (list_link_is_linked(&(child_proc->p_child_link)))
                                                list_remove(&(child_proc->p_child_link));
                                        if (list_link_is_linked(&(child_proc->p_list_link)))
                                                list_remove(&(child_proc->p_list_link));

                                        if (child_proc->p_vmmap != NULL) {
                                                vmmap_destroy(child_proc->p_vmmap);
                                                child_proc->p_vmmap = NULL;
                                        }
                                        pt_destroy_pagedir(child_proc->p_pagedir);
                                        slab_obj_free(proc_allocator, child_proc);

                                        return reaped_pid;
                                }
                        }
                } list_iterate_end();

                if (!matching_child_exists) {
                        return -ECHILD;
                }

                sched_sleep_on(&(curproc->p_wait));
        }
}
/*
 * Cancel all threads and join with them (if supporting MTP), and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
        kthread_exit((void *)(intptr_t)status);

     
        panic("do_exit: returned unexpectedly");
}

