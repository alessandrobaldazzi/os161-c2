
#include <types.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <uio.h>
#include <vnode.h>
#include <elf.h>
#include <stat.h>
#include <synch.h>
#include <kern/errno.h>
#include "opt-shell.h"




int sys_write(int fd, userptr_t buf_ptr, size_t size, int* err){
	int i, res, ret;
	char *p = (char *)buf_ptr; 
	char namebuffer[PATH_MAX];
	int is_ref; 	//has proc_file_table[fd] a reference to the system_wide?
	struct proc *pr = curproc;
	struct iovec iov;
	struct uio u;
	struct addrspace *as = proc_getas();
	struct vnode *v;

	res = copyin((userptr_t) buf_ptr, namebuffer, size);
	if(res){
		*err = res;
		return -1;
	}

	if(fd < 0 || fd >= OPEN_MAX){
		*err = EBADF;
		return -1;
	}

	if(pr->process_file_table[fd] == NULL){
		*err = EBADF;
		return -1;
	}

	if(buf_ptr == NULL){
		*err = EFAULT;
		return -1;
	}
	
	if(pr->process_file_table[fd]->of_ref != NULL)
		is_ref=1;
	else{
		is_ref=0;
	}



//we are tring to write onto a STD FILE -> use putch
  	if (!is_ref){
  		for (i=0; i<(int)size; i++) {
  			putch(p[i]);
  		}

 		 return (int)size;
  	 }
	
	//not STD FILES -> check the flags
	res = pr->process_file_table[fd]->flag & O_ACCMODE;
	kprintf("\n %d",res);
	if(res != O_WRONLY && res != O_RDWR){
		*err = EBADF;
		return -1;
	}


//manual initialization of iov and uio -> we are in userspace

 	iov.iov_ubase = buf_ptr;
  	iov.iov_len = size;      

  	u.uio_iov = &iov;
 	u.uio_iovcnt = 1;
  	u.uio_resid = size;   

 	u.uio_offset = pr->process_file_table[fd]->offset;



 	u.uio_segflg = UIO_USERSPACE;
 	u.uio_rw = UIO_WRITE;
 	u.uio_space = as;

	lock_acquire(pr->process_file_table[fd]->of_ref->p_lock);

	v = pr->process_file_table[fd]->of_ref->vn;
 	res = VOP_WRITE(v, &u);
	if(res){
		lock_release(pr->process_file_table[fd]->of_ref->p_lock);
		*err = res;
		return -1;
	}	
	pr->process_file_table[fd]->offset = u.uio_offset;	
	ret = size - u.uio_resid; 

	lock_release(pr->process_file_table[fd]->of_ref->p_lock);

  	return ret;
}

int sys_read(int fd, userptr_t buf_ptr, size_t size, int* err){

  	int i, res, ret;
  	char *p = (char *)buf_ptr;
	char namebuffer[PATH_MAX];
	int is_ref;  	
  	struct proc *pr = curproc;
  	struct iovec iov;
  	struct uio u;
  	struct addrspace *as;
  	struct vnode *v;
	
 	res = copyin((userptr_t) buf_ptr, namebuffer, size);
	if(res){
		*err = res;
		return -1;
	}

	if(fd < 0 || fd >= OPEN_MAX){
		*err = EBADF;
		return -1;
	}

	if(pr->process_file_table[fd] == NULL){
		*err = EBADF;
		return -1;
	}

	if(buf_ptr == NULL){
		*err = EFAULT;
		return -1;
	}
	
	if(pr->process_file_table[fd]->of_ref != NULL)
		is_ref=1;
	else{
		is_ref=0;
	}

	

	if (!is_ref){
 	 	for (i=0; i<(int)size; i++) {
   		 	p[i] = getch();
    			if (p[i] < 0) 
     				return i;
  		}
	
		return size;
   	}

	res = pr->process_file_table[fd]->flag & O_ACCMODE;
	if(res != O_RDONLY && res != O_RDWR){
		*err = EBADF;
		return -1;
	}

  	iov.iov_ubase = buf_ptr;
  	iov.iov_len = size;      
  	as = proc_getas();

  	u.uio_iov = &iov;
  	u.uio_iovcnt = 1;
  	u.uio_resid = size;   

  	u.uio_offset = pr->process_file_table[fd]->offset;


  	u.uio_segflg = UIO_USERSPACE;
  	u.uio_rw = UIO_READ;
  	u.uio_space = as;

	lock_acquire(pr->process_file_table[fd]->of_ref->p_lock);
	v = pr->process_file_table[fd]->of_ref->vn;
  	res = VOP_READ(v, &u);

	if(res){
		lock_release(pr->process_file_table[fd]->of_ref->p_lock);
		*err = res;
		return -1;
	}	
	pr->process_file_table[fd]->offset = u.uio_offset;	
	ret = size - u.uio_resid; 

	lock_release(pr->process_file_table[fd]->of_ref->p_lock);

  	return ret;
}

off_t sys_lseek(int fd, off_t offset, int whence, int *err){
    
	off_t finaloffset;
    struct stat file_stat;
    struct proc *current_process = curproc;

    if (fd < 0 || fd >= OPEN_MAX){
        *err = EBADF;
        return -1;
    }
    if (current_process->process_file_table[fd] == NULL){
        *err = EBADF;
        return -1;
    }
    if (current_process->process_file_table[fd]->of_ref == NULL){
        *err = ESPIPE;
        return -1;
    }
    if (current_process->process_file_table[fd]->of_ref->vn == NULL){
        *err = ESPIPE;
        return -1;
    }
    //VOP_ISSEEKABLE is in vnode.h and return true if the file is seekable. It means that it's not a file, but a pipe
    if (!VOP_ISSEEKABLE(current_process->process_file_table[fd]->of_ref->vn)){
        *err = ESPIPE;
        return -1;
    }
    //VOP_STAT returns info about the file: we can use it to knoe the size of the file.
    if (VOP_STAT(current_process->process_file_table[fd]->of_ref->vn, &file_stat))
        return -1;

    switch (whence){

		case SEEK_SET:
			finaloffset = offset;
			if (finaloffset < 0){
				*err = EINVAL;
				return -1;
			}
			break;

		case SEEK_CUR:
			finaloffset = current_process->process_file_table[fd]->offset + offset;
			if (finaloffset < 0){
				*err = EINVAL;
				return -1;
			}
			break;

		case SEEK_END:
			finaloffset = file_stat.st_size + offset;
			if (finaloffset < 0){
				*err = EINVAL;
				return -1;
			}
			break;

		default:
			*err = EINVAL;
			return -1;
	}

    current_process->process_file_table[fd]->offset = finaloffset;
    return finaloffset;
}

int sys_chdir(const char *pathname, int *err){
    int res;
    char pathkernel[PATH_MAX];
    size_t path_length;

    res = copyinstr((const_userptr_t)pathname, pathkernel, PATH_MAX, &path_length);
    if (res){
        *err = res;
        return -1;
    }
    res = vfs_chdir((char *)pathkernel);
    if (res){
        *err = res;
        return -1;
    }
    return 0;
}

int sys_open(char *pathname, int flag, int *err){
    int res, flag_masked, fd;
    struct proc *current_process = curproc;
    size_t size;
    struct vnode *vn;
    char path[PATH_MAX];

    if (pathname == NULL){
        *err = EFAULT;
        return -1;
    }

    res = copyinstr((userptr_t)pathname, path, PATH_MAX, &size);
    if (res){
        *err = res;
        return -1;
    }

    flag_masked = flag & O_ACCMODE;
    if ((flag_masked != O_RDWR && flag_masked != O_RDONLY && flag_masked != O_WRONLY) || flag > 255){
        *err = EINVAL;
        return -1;
    }

    mode_t mode =0;
    *err = vfs_open(path, flag, mode, &vn);
    if (*err){
        return -1;
    }

    fd = assign_fd(current_process, vn, flag, err);
    if (fd){
        current_process->process_file_table[fd]->flag = flag;
        return fd;
    }
    
	return -1;
}
int sys__getcwd(char *buf, size_t buflen, int *err){
	
	int res;
	char path[PATH_MAX];
	size_t path_len;

	res = copyinstr((const_userptr_t)buf, path, PATH_MAX, &path_len);

	if (res){
		*err = res;
		return -1;
	}

	//userspace->manually init
	struct iovec iov;
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;
	struct uio u;

	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;
	u.uio_offset = 0;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace;

	res = vfs_getcwd(&u);
	if (res){
		*err = ENOENT;
		return -1;
	}

	return buflen;
}

int sys_dup2(int oldfd,int newfd,int *err){


	if(oldfd<0 || oldfd>=OPEN_MAX){
		*err=EBADF;
		return -1;
	}

	if(newfd<0 || newfd>=OPEN_MAX){
		*err=EBADF;
		return -1;
	}
	
	struct proc *current_proc = curproc;
	if(current_proc->process_file_table[oldfd]==NULL){
		*err=EBADF;
		return -1;
	}
	
	if(current_proc->cnt_open >= OPEN_MAX){
    		*err = EMFILE;
    		return -1; 
 	}

 	if(newfd==oldfd)
 		return newfd;

	if(current_proc->process_file_table[newfd]!=NULL){ //newfd is already used->close the old file
		sys_close(newfd,err);
	}

	current_proc->process_file_table[newfd]=current_proc->process_file_table[oldfd];
	if(current_proc->process_file_table[newfd]->of_ref!=NULL){
	//*openfile != NULL -> vnode(file) is already used -> shared between more processes.
		lock_acquire(current_proc->process_file_table[newfd]->of_ref->p_lock);
		VOP_INCREF(current_proc->process_file_table[newfd]->of_ref->vn);
		current_proc->process_file_table[newfd]->of_ref->of_pointercount++;
		lock_release(current_proc->process_file_table[newfd]->of_ref->p_lock);	
	}
	
	return newfd;

}

int sys_close(int fd, int*err){
    struct vnode *vn;
    struct proc *current_process = curproc;

    if(fd<0 || fd>=OPEN_MAX){
        *err=EBADF;
        return -1;
    }
    if(current_process->process_file_table[fd]==NULL){
        *err=EBADF;
        return -1;
    }
//when doing a dup, it can happen that the previous fd is related a STDIN,STDOUT e STDERR (0,1,2).
    if(current_process->process_file_table[fd]->of_ref == NULL){
		current_process->process_file_table[fd]=NULL;
		current_process->cnt_open--;
		current_process->last_fd = fd;
		return 0;
    }

    vn = current_process->process_file_table[fd]->of_ref->vn;
    remove_fd(current_process, fd);
    vfs_close(vn);
    return 0;    
}


