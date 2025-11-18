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

#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
        uint32_t low_page_number;
        file_t *target_file;
        vmarea_t *new_vmarea;
        int map_result;
        int is_anon_mapping;
        int is_fixed_mapping;
        uint32_t page_count;
        uintptr_t mapped_start_addr;
        uint32_t aligned_length;

        is_fixed_mapping = (flags & MAP_FIXED);
        is_anon_mapping = (flags & MAP_ANON);

        if (is_fixed_mapping) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                if (USER_MEM_LOW > (unsigned int)addr || 
                    USER_MEM_HIGH < (unsigned int)addr + len) {
                        dbg(DBG_PRINT, "(GRADING3D)\n");
                        return -EINVAL;
                }
                low_page_number = ADDR_TO_PN(addr);
        } else {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                low_page_number = 0;
        }

        if (!PAGE_ALIGNED(off) || len <= 0 || 
            len > (USER_MEM_HIGH - USER_MEM_LOW)) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        if (!((flags & MAP_SHARED) ^ (flags & MAP_PRIVATE))) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        target_file = NULL;
        new_vmarea = NULL;
        page_count = (len - 1) / PAGE_SIZE + 1;

        if (!is_anon_mapping) {
                if (fd < 0 || fd >= NFILES) {
                        dbg(DBG_PRINT, "(GRADING3D)\n");
                        return -EBADF;
                }

                target_file = fget(fd);
                if (target_file == NULL) {
                        dbg(DBG_PRINT, "(GRADING3D)\n");
                        return -EBADF;
                }

                if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && 
                    (target_file->f_mode == FMODE_READ)) {
                        dbg(DBG_PRINT, "(GRADING3D)\n");
                        fput(target_file);
                        return -EINVAL;
                }

                map_result = vmmap_map(curproc->p_vmmap, target_file->f_vnode, 
                                      low_page_number, page_count, prot, 
                                      flags, off, VMMAP_DIR_HILO, &new_vmarea);
                dbg(DBG_PRINT, "(GRADING3B)\n");
                fput(target_file);
        } else {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                map_result = vmmap_map(curproc->p_vmmap, NULL, low_page_number, 
                                      page_count, prot, flags, 
                                      off, VMMAP_DIR_HILO, &new_vmarea);
        }

        if (map_result < 0) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return map_result;
        }

        *ret = PN_TO_ADDR(new_vmarea->vma_start);
        mapped_start_addr = (uintptr_t)PN_TO_ADDR(new_vmarea->vma_start);
	aligned_length = (uint32_t)((len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

        pt_unmap_range(curproc->p_pagedir, mapped_start_addr, 
                      mapped_start_addr + aligned_length);
        tlb_flush_range(mapped_start_addr, aligned_length / PAGE_SIZE);

        KASSERT(NULL != curproc->p_pagedir);
        dbg(DBG_PRINT, "(GRADING3A 2.a)\n");

        return map_result;
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
        uint32_t low_page_number;
        uint32_t page_count;
        unsigned int start_addr_uint;
        unsigned int end_addr_uint;

        start_addr_uint = (unsigned int)addr;
        end_addr_uint = start_addr_uint + len;

        if (start_addr_uint < USER_MEM_LOW) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        if (end_addr_uint > USER_MEM_HIGH) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        if (len <= 0 || len > (USER_MEM_HIGH - USER_MEM_LOW)) {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        low_page_number = ADDR_TO_PN(addr);
        page_count = (len - 1) / PAGE_SIZE + 1;
	vmmap_remove(curproc->p_vmmap, low_page_number, page_count);
        dbg(DBG_PRINT, "(GRADING3B)\n");
        tlb_flush_all();

        return 0;
}

