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
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{
        proc_t *new_child_proc;
        vmmap_t *child_vmmap;
        vmarea_t *child_vmarea;
        vmarea_t *parent_vmarea;
        kthread_t *child_thread;
        mmobj_t *parent_shadow_obj;
        mmobj_t *child_shadow_obj;
        mmobj_t *original_obj;
        mmobj_t *bottom_obj;
        int file_index;
        uint32_t child_stack_ptr;
        int is_private_mapping;

        KASSERT(regs != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc->p_state == PROC_RUNNING);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        dbg(DBG_PRINT, "(GRADING3B)\n");

        new_child_proc = proc_create("child_proc");
        KASSERT(new_child_proc != NULL);

        child_vmmap = vmmap_clone(curproc->p_vmmap);
        KASSERT(child_vmmap != NULL);

        new_child_proc->p_vmmap = child_vmmap;
        child_vmmap->vmm_proc = new_child_proc;

        list_iterate_begin(&child_vmmap->vmm_list, child_vmarea, vmarea_t, vma_plink) {
                parent_vmarea = vmmap_lookup(curproc->p_vmmap, child_vmarea->vma_start);
                KASSERT(parent_vmarea != NULL);

                original_obj = parent_vmarea->vma_obj;
                is_private_mapping = ((child_vmarea->vma_flags & MAP_TYPE) == MAP_PRIVATE);

                if (is_private_mapping) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        
                        original_obj->mmo_ops->ref(original_obj);

                        parent_shadow_obj = shadow_create();
                        parent_shadow_obj->mmo_shadowed = original_obj;
                        bottom_obj = mmobj_bottom_obj(original_obj);
                        parent_shadow_obj->mmo_un.mmo_bottom_obj = bottom_obj;

                        child_shadow_obj = shadow_create();
                        child_shadow_obj->mmo_shadowed = original_obj;
                        child_shadow_obj->mmo_un.mmo_bottom_obj = bottom_obj;

                        parent_vmarea->vma_obj = parent_shadow_obj;
                        child_vmarea->vma_obj = child_shadow_obj;
                } else {
                        dbg(DBG_ANON, "(GRADING3B)\n");
                        child_vmarea->vma_obj = original_obj;
                        original_obj->mmo_ops->ref(original_obj);
                }

                list_insert_tail(mmobj_bottom_vmas(parent_vmarea->vma_obj), &child_vmarea->vma_olink);
        } list_iterate_end();

        child_thread = kthread_clone(curthr);
        KASSERT(child_thread != NULL);
        child_thread->kt_proc = new_child_proc;

        KASSERT(new_child_proc->p_state == PROC_RUNNING);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(new_child_proc->p_pagedir != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(child_thread->kt_kstack != NULL);
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        list_insert_tail(&new_child_proc->p_threads, &child_thread->kt_plink);

        regs->r_eax = 0;
        child_thread->kt_ctx.c_pdptr = new_child_proc->p_pagedir;
        child_thread->kt_ctx.c_eip = (uint32_t) userland_entry;

        child_stack_ptr = fork_setup_stack(regs, child_thread->kt_kstack);
        child_thread->kt_ctx.c_esp = child_stack_ptr;
        child_thread->kt_ctx.c_kstack = (uintptr_t) child_thread->kt_kstack;
        child_thread->kt_ctx.c_kstacksz = DEFAULT_STACK_SIZE;

        for (file_index = 0; file_index < NFILES; file_index++) {
                new_child_proc->p_files[file_index] = curproc->p_files[file_index];
                if (new_child_proc->p_files[file_index] != NULL) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        fref(new_child_proc->p_files[file_index]);
                } else {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                }
        }

        pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
        tlb_flush_all();

        new_child_proc->p_brk = curproc->p_brk;
        new_child_proc->p_start_brk = curproc->p_start_brk;

        sched_make_runnable(child_thread);
        regs->r_eax = new_child_proc->p_pid;
        return new_child_proc->p_pid;
}
