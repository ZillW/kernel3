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
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
        KASSERT(NULL != dir); dbg(DBG_PRINT, "(GRADING2A 2.a)\n");
        KASSERT(NULL != name); dbg(DBG_PRINT, "(GRADING2A 2.a)\n");
        KASSERT(NULL != result); dbg(DBG_PRINT, "(GRADING2A 2.a)\n");

        if (dir->vn_ops == NULL || dir->vn_ops->lookup == NULL) {dbg(DBG_PRINT, "(GRADING2B)\n"); return -ENOTDIR;}
        return dir->vn_ops->lookup(dir, name, len, result);
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
        KASSERT(pathname != NULL); dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        KASSERT(namelen  != NULL); dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        KASSERT(name     != NULL); dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        KASSERT(res_vnode!= NULL); dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
vnode_t *current_dir;
        
        if(pathname[0] == '/'){
                current_dir = vfs_root_vn;
		dbg(DBG_PRINT, "(GRADING2B)\n");
        } else {
                current_dir = (base == NULL) ? curproc->p_cwd : base;
		dbg(DBG_PRINT, "(GRADING2B)\n");
        }

        KASSERT(NULL != current_dir);
        dbg(DBG_PRINT, "(GRADING2A 2.b)\n");

        int last_char_idx = (int)strlen(pathname) - 1;
        while(last_char_idx >= 0 && pathname[last_char_idx] == '/') {
                last_char_idx--;
        }

        if (last_char_idx < 0 && pathname[0] == '/') {
                vref(current_dir);
                *namelen = 1;
                *name = ".";
                *res_vnode = current_dir;
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return 0;
        }

        vref(current_dir);
        vnode_t *next_node = NULL;
        int lookup_status = 0;
        
        int path_iter = 0;
        int component_start = 0;
        int component_end = 0;
        int final_name_len = 0;
        int has_component_chars = 0; /* Flag to handle multiple slashes */

        while(pathname[path_iter] != '\0' && path_iter <= last_char_idx) {
                
                if(pathname[component_end] != '/') {
                        component_end++;
                        has_component_chars = 1;
                        final_name_len = component_end - component_start;
			dbg(DBG_PRINT, "(GRADING2B)\n");
                } else {
                        int current_len = component_end - component_start;
			dbg(DBG_PRINT, "(GRADING2B)\n");            
            
                        if(current_len > NAME_LEN) {
                                vput(current_dir);
				dbg(DBG_PRINT, "(GRADING2B)\n");
                                return -ENAMETOOLONG;
                        }

                        if(has_component_chars) {     
                                lookup_status = lookup(current_dir, pathname + component_start, current_len, &next_node);    
                                
                                vput(current_dir); /* Release ref on parent */

                                if(lookup_status != 0){
					dbg(DBG_PRINT, "(GRADING2B)\n");
                                        return lookup_status;
                                }
                                
                                KASSERT(NULL != next_node);
				dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
                                current_dir = next_node; /* Move to child */
                                has_component_chars = 0;
                        }

                        /* Skip over the slash(es) */
                        component_end++;
                        component_start = component_end;
                }
                
                path_iter++;
        }

        if(final_name_len > NAME_LEN){
                vput(current_dir);
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return -ENAMETOOLONG;
        }

        *namelen = final_name_len;
        *name = pathname + component_start;
        *res_vnode = current_dir;
	dbg(DBG_PRINT, "(GRADING2B)\n");

        return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fcntl.h>.  If the O_CREAT flag is specified and the file does
 * not exist, call create() in the parent directory vnode. However, if the
 * parent directory itself does not exist, this function should fail - in all
 * cases, no files or directories other than the one at the very end of the path
 * should be created.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
        size_t namelen = 0;
        const char *name = NULL;
        vnode_t *dir;

	int trailing_slash = (pathname[strlen(pathname) - 1] == '/');

        int rc = dir_namev(pathname, &namelen, &name, base, &dir);
        if (rc < 0) {dbg(DBG_PRINT, "(GRADING2B)\n"); return rc;}

        if (namelen == 0) {
                vput(dir);
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return -ENOENT;
        }

        rc = lookup(dir, name, namelen, res_vnode);
        if (rc != 0) {
                if ((flag & O_CREAT) && rc == -ENOENT) {
                        KASSERT(NULL != dir->vn_ops->create);
                        dbg(DBG_PRINT, "(GRADING2A 2.c)\n");
			dbg(DBG_PRINT, "(GRADING2B)\n");
                        rc = dir->vn_ops->create(dir, name, namelen, res_vnode);
                        vput(dir);
                        return rc;
                }
                vput(dir);
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return rc;
        }

	if (trailing_slash && !S_ISDIR((*res_vnode)->vn_mode)) {
                vput(*res_vnode); 
                vput(dir);
		dbg(DBG_PRINT, "(GRADING2B)\n");        
                return -ENOTDIR;  
        }

        vput(dir);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
