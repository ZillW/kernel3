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
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"
#include "mm/tlb.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        slab_obj_free(vmarea_allocator, vma);
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n",
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        /*
        KASSERT(0 <= size);
        if (0 == size) {
                size++;
                buf--;
                buf[0] = '\0';
        }
        */
        return osize - size;
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{
        vmmap_t *new_address_space;

        dbg(DBG_PRINT, "(GRADING3B 1)\n");
        new_address_space = (vmmap_t *)slab_obj_alloc(vmmap_allocator);
        if (new_address_space == NULL) {
                return NULL;
        }
        
        list_init(&(new_address_space->vmm_list));
        new_address_space->vmm_proc = NULL;
  
        return new_address_space;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{
        vmarea_t *current_vmarea;

        KASSERT(NULL != map);
        dbg(DBG_PRINT, "(GRADING3A 3.a)\n");

        list_iterate_begin(&map->vmm_list, current_vmarea, vmarea_t, vma_plink) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                list_remove(&(current_vmarea->vma_plink));   
                list_remove(&(current_vmarea->vma_olink)); 
                current_vmarea->vma_obj->mmo_ops->put(current_vmarea->vma_obj);
                current_vmarea->vma_obj = NULL;
                vmarea_free(current_vmarea);
        } list_iterate_end();
  
        slab_obj_free(vmmap_allocator, map);
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{
        vmarea_t *existing_vmarea;
        vmarea_t *insertion_point;

        KASSERT(NULL != map && NULL != newvma);
        KASSERT(NULL == newvma->vma_vmmap);
        KASSERT(newvma->vma_start < newvma->vma_end);
        KASSERT(ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end);
        dbg(DBG_PRINT, "(GRADING3A 3.b)\n");

        newvma->vma_vmmap = map;
        insertion_point = NULL;
        
        list_iterate_begin(&map->vmm_list, existing_vmarea, vmarea_t, vma_plink) {
                if (existing_vmarea->vma_start >= newvma->vma_start) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        insertion_point = existing_vmarea;
                        list_insert_before(&insertion_point->vma_plink, &newvma->vma_plink);
                        return;
                }
        } list_iterate_end();
        
        dbg(DBG_PRINT, "(GRADING3B)\n");
        list_insert_tail(&map->vmm_list, &newvma->vma_plink);
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{
        //NOT_YET_IMPLEMENTED("VM: vmmap_find_range");
        uint32_t start, end;
        uint32_t best_gap_start = 0;
        uint32_t best_gap_size = 0;
        vmarea_t *vma;
        
        if (dir == VMMAP_DIR_LOHI) {
                /* Find gap as low as possible */
                start = ADDR_TO_PN(USER_MEM_LOW);
                list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                        end = vma->vma_start;
                        if (end - start >= npages) {
                                return start;
                        }
                        start = vma->vma_end;
                } list_iterate_end();
                end = ADDR_TO_PN(USER_MEM_HIGH);
                if (end - start >= npages) {
                        return start;
                }
        } else {
                /* Find gap as high as possible - iterate forward and track best gap */
                start = ADDR_TO_PN(USER_MEM_LOW);
                list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                        end = vma->vma_start;
                        if (end > start && (end - start) >= npages) {
                                if ((end - start) > best_gap_size) {
                                        best_gap_start = start;
                                        best_gap_size = end - start;
                                }
                        }
                        start = vma->vma_end;
                } list_iterate_end();
                end = ADDR_TO_PN(USER_MEM_HIGH);
                if (end > start && (end - start) >= npages) {
                        if ((end - start) > best_gap_size) {
                                best_gap_start = start;
                                best_gap_size = end - start;
                        }
                }
                
                if (best_gap_size >= npages) {
                        /* Return the highest address that fits */
                        return best_gap_start + (best_gap_size - npages);
                }
        }
        
        return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{
        vmarea_t *current_vmarea;
        int is_in_range;

        KASSERT(NULL != map);
        dbg(DBG_PRINT, "(GRADING3A 3.c)\n");
  
        list_iterate_begin(&map->vmm_list, current_vmarea, vmarea_t, vma_plink) {
                is_in_range = (vfn >= current_vmarea->vma_start) && (vfn < current_vmarea->vma_end);
                if (is_in_range) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        return current_vmarea;
                }
        } list_iterate_end();

        dbg(DBG_PRINT, "(GRADING3C)\n");
  
        return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
        vmmap_t* new_vmmap = vmmap_create();

  
        vmarea_t* vma = NULL;
  
        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                vmarea_t* vm = vmarea_alloc();
                vm->vma_start = vma->vma_start;
                vm->vma_end = vma->vma_end;
                vm->vma_off = vma->vma_off;
                vm->vma_prot = vma->vma_prot;
                vm->vma_flags = vma->vma_flags;
                vm->vma_vmmap = new_vmmap;
  
                list_link_init(&vm->vma_plink);
                list_link_init(&vm->vma_olink);                
  
                list_insert_tail(&new_vmmap->vmm_list, &vm->vma_plink);
  
        } list_iterate_end();
        dbg(DBG_PRINT, "(GRADING3B)\n");
        return new_vmmap;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
        vmarea_t *new_vmarea;
        uint32_t target_start_page;
        int range_find_result;
        int is_range_occupied;
        mmobj_t *memory_object;
        mmobj_t *shadow_object;
        mmobj_t *bottom_memory_object;
        int mmap_operation_result;
        int is_private_mapping;
        int is_shared_mapping;

        KASSERT(NULL != map);
        KASSERT(0 < npages);
        KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags));
        KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage));
        KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages)));
        KASSERT(PAGE_ALIGNED(off));
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

        new_vmarea = vmarea_alloc();
        if (new_vmarea == NULL) {
                return -ENOMEM;
        }

        target_start_page = lopage;
        if (target_start_page == 0) {
                range_find_result = vmmap_find_range(map, npages, dir);
                if (range_find_result == -1) {
                        dbg(DBG_PRINT, "(GRADING3C)\n");
                        vmarea_free(new_vmarea);
                        return -ENOMEM;
                }
                dbg(DBG_PRINT, "(GRADING3B)\n");
                target_start_page = range_find_result;
        } else {
                is_range_occupied = !vmmap_is_range_empty(map, target_start_page, npages);
                if (is_range_occupied) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        vmmap_remove(map, target_start_page, npages);
                } else {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                }
        }
  
        new_vmarea->vma_start = target_start_page;
        new_vmarea->vma_end = target_start_page + npages;
        new_vmarea->vma_off = ADDR_TO_PN(off);
        new_vmarea->vma_prot = prot;
        new_vmarea->vma_flags = flags;
        new_vmarea->vma_obj = NULL;
        list_link_init(&new_vmarea->vma_plink);
        list_link_init(&new_vmarea->vma_olink);
  
        memory_object = NULL;
        if (file == NULL) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                memory_object = anon_create();
                if (memory_object == NULL) {
                        vmarea_free(new_vmarea);
                        return -ENOMEM;
                }
        } else {
                mmap_operation_result = file->vn_ops->mmap(file, new_vmarea, &memory_object);
                if (mmap_operation_result < 0 || memory_object == NULL) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        vmarea_free(new_vmarea);
                        return mmap_operation_result < 0 ? mmap_operation_result : -ENOMEM;
                }
                dbg(DBG_PRINT, "(GRADING3B)\n");
        }
          
        is_private_mapping = (flags & MAP_PRIVATE) != 0;
        is_shared_mapping = (flags & MAP_SHARED) != 0;
        
        if (is_private_mapping) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                shadow_object = shadow_create();
                if (shadow_object == NULL) {
                        memory_object->mmo_ops->put(memory_object);
                        vmarea_free(new_vmarea);
                        return -ENOMEM;
                }
  
                shadow_object->mmo_shadowed = memory_object;
                new_vmarea->vma_obj = shadow_object;
                
                bottom_memory_object = mmobj_bottom_obj(memory_object);
                shadow_object->mmo_un.mmo_bottom_obj = bottom_memory_object;
                list_insert_head(&(bottom_memory_object->mmo_un.mmo_vmas), &(new_vmarea->vma_olink));
        } else if (is_shared_mapping) {
                dbg(DBG_PRINT, "(GRADING3C)\n");
                new_vmarea->vma_obj = memory_object;  
                list_insert_head(&(memory_object->mmo_un.mmo_vmas), &(new_vmarea->vma_olink));
        }
        
        vmmap_insert(map, new_vmarea);
        if (new != NULL) {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                *new = new_vmarea;
        } else {
                dbg(DBG_PRINT, "(GRADING3B)\n");
        }
        return 0;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages)
{
uint32_t unmap_end = lopage + npages;

    list_link_t *current_link = map->vmm_list.l_next;
    list_link_t *next_link = NULL;

    while (current_link != &map->vmm_list) {
        
        vmarea_t *current_vma = list_item(current_link, vmarea_t, vma_plink);
        
        next_link = current_link->l_next;

        uint32_t vma_start = current_vma->vma_start;
        uint32_t vma_end = current_vma->vma_end;

        if (vma_start < lopage && vma_end > unmap_end) {
            dbg(DBG_PRINT, "(GRADING3C)\n");
            
            vmarea_t* new_vma_back = vmarea_alloc();
            new_vma_back->vma_start = unmap_end;
            new_vma_back->vma_end = vma_end;
            new_vma_back->vma_off = current_vma->vma_off + (unmap_end - vma_start);
            new_vma_back->vma_prot = current_vma->vma_prot;
            new_vma_back->vma_flags = current_vma->vma_flags;
            new_vma_back->vma_vmmap = map;

            mmobj_t *top_obj = current_vma->vma_obj;
            new_vma_back->vma_obj = top_obj;

            top_obj->mmo_ops->ref(top_obj);

            current_vma->vma_end = lopage;
            
            list_insert_before(next_link, &new_vma_back->vma_plink);

            mmobj_t *bottom_obj = mmobj_bottom_obj(top_obj);
            list_insert_tail(mmobj_bottom_vmas(bottom_obj), &new_vma_back->vma_olink);
        }
        else if (vma_start < lopage && vma_end > lopage && vma_end <= unmap_end) {
            dbg(DBG_PRINT, "(GRADING3C)\n");
            current_vma->vma_end = lopage;
        }
        else if (vma_start >= lopage && vma_start < unmap_end && vma_end > unmap_end) {
            dbg(DBG_PRINT, "(GRADING3C)\n");
            current_vma->vma_off += (unmap_end - vma_start);
            current_vma->vma_start = unmap_end;
        }
        else if (vma_start >= lopage && vma_end <= unmap_end) {
            dbg(DBG_PRINT, "(GRADING3B)\n");
            
            list_remove(&current_vma->vma_plink);
            list_remove(&current_vma->vma_olink);
            current_vma->vma_obj->mmo_ops->put(current_vma->vma_obj);
            current_vma->vma_obj = NULL;
            vmarea_free(current_vma);
        }
        else {
            dbg(DBG_PRINT, "(GRADING3B)\n");
        }

        current_link = next_link;

    }

    dbg(DBG_PRINT, "(GRADING3B)\n");
    tlb_flush_all();
    proc_t *owner = map->vmm_proc;
if (owner && owner->p_pagedir) {
    pt_unmap_range(owner->p_pagedir,
                   (uintptr_t)PN_TO_ADDR(lopage),
                   (uintptr_t)PN_TO_ADDR(lopage + npages));
}
    return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
        vmarea_t *current_vmarea;
        uint32_t end_vfn;
        int has_overlap;

        end_vfn = startvfn + npages;
        KASSERT((startvfn < end_vfn) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= end_vfn));
        dbg(DBG_PRINT, "(GRADING3A 3.e)\n");

        list_iterate_begin(&map->vmm_list, current_vmarea, vmarea_t, vma_plink) {
                has_overlap = !((current_vmarea->vma_start >= end_vfn) || (current_vmarea->vma_end <= startvfn));
                if (has_overlap) {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        return 0;
                }
        } list_iterate_end();        
  
        dbg(DBG_PRINT, "(GRADING3B)\n");

        return 1;
}


/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{

    if (count == 0) return 0;
    if (vaddr == NULL || buf == NULL) return -EFAULT;

    uintptr_t start_addr = (uintptr_t)vaddr;
    uint32_t start_vfn   = ADDR_TO_PN(start_addr);
    uint32_t start_offset = PAGE_OFFSET(start_addr);

    uintptr_t end_addr   = start_addr + count - 1;
    uint32_t end_vfn     = ADDR_TO_PN(end_addr);

    if (start_vfn == end_vfn) {
        vmarea_t *vma = vmmap_lookup(map, start_vfn);
        if (!vma) return -EFAULT;
        if (vma->vma_obj == NULL) return -EFAULT;

        pframe_t *pf = NULL;
        int ret = pframe_lookup(vma->vma_obj,
                                vma->vma_off + (start_vfn - vma->vma_start),
                                0,
                                &pf);
        if (ret != 0) return ret;
        if (pf == NULL || pf->pf_addr == NULL) return -EFAULT;

        memcpy(buf, (const char *)pf->pf_addr + start_offset, count);
        return 0;
    }

    size_t bytes_done   = 0;
    uint32_t current_vfn = start_vfn;

    while (current_vfn <= end_vfn) {
        vmarea_t *vma = vmmap_lookup(map, current_vfn);
        if (!vma) return -EFAULT;
        if (vma->vma_obj == NULL) return -EFAULT;

        pframe_t *pf = NULL;
        int ret = pframe_lookup(vma->vma_obj,
                                vma->vma_off + (current_vfn - vma->vma_start),
                                0,
                                &pf);
        if (ret != 0) return ret;
        if (pf == NULL || pf->pf_addr == NULL) return -EFAULT;

        size_t copy_len;
        if (current_vfn == start_vfn) {
            copy_len = PAGE_SIZE - start_offset;
        } else if (current_vfn == end_vfn) {
            copy_len = count - bytes_done;
        } else {
            copy_len = PAGE_SIZE;
        }

        memcpy((char *)buf + bytes_done,
               (const char *)pf->pf_addr + ((current_vfn == start_vfn) ? start_offset : 0),
               copy_len);

        bytes_done   += copy_len;
        current_vfn  += 1;
    }

    return 0;
}

/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
    if (count == 0) return 0;
    if (vaddr == NULL || buf == NULL) return -EFAULT;

    uintptr_t start_addr = (uintptr_t)vaddr;
    uint32_t start_vfn   = ADDR_TO_PN(start_addr);
    uint32_t start_offset = PAGE_OFFSET(start_addr);

    uintptr_t end_addr   = start_addr + count - 1;
    uint32_t end_vfn     = ADDR_TO_PN(end_addr);

    if (start_vfn == end_vfn) {
        vmarea_t *vma = vmmap_lookup(map, start_vfn);
        if (!vma) return -EFAULT;
        if (vma->vma_obj == NULL) return -EFAULT;

        pframe_t *pf = NULL;
        int ret = pframe_lookup(vma->vma_obj,
                                vma->vma_off + (start_vfn - vma->vma_start),
                                1,
                                &pf);
        if (ret != 0) return ret;
        if (pf == NULL || pf->pf_addr == NULL) return -EFAULT;

        memcpy((char *)pf->pf_addr + start_offset, buf, count);
        pframe_dirty(pf);
        return 0;
    }

    size_t bytes_done   = 0;
    uint32_t current_vfn = start_vfn;

    while (current_vfn <= end_vfn) {
        vmarea_t *vma = vmmap_lookup(map, current_vfn);
        if (!vma) return -EFAULT;
        if (vma->vma_obj == NULL) return -EFAULT;

        pframe_t *pf = NULL;
        int ret = pframe_lookup(vma->vma_obj,
                                vma->vma_off + (current_vfn - vma->vma_start),
                                1,
                                &pf);
        if (ret != 0) return ret;
        if (pf == NULL || pf->pf_addr == NULL) return -EFAULT;

        size_t copy_len;
        if (current_vfn == start_vfn) {
            copy_len = PAGE_SIZE - start_offset;
            memcpy((char *)pf->pf_addr + start_offset,
                   (const char *)buf + bytes_done,
                   copy_len);
        } else if (current_vfn == end_vfn) {
            copy_len = count - bytes_done;
            memcpy((char *)pf->pf_addr,
                   (const char *)buf + bytes_done,
                   copy_len);
        } else {
            copy_len = PAGE_SIZE;
            memcpy((char *)pf->pf_addr,
                   (const char *)buf + bytes_done,
                   copy_len);
        }

        pframe_dirty(pf);
        bytes_done  += copy_len;
        current_vfn += 1;
    }

    return 0;
}
