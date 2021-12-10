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


int sys_write(int fd, userptr_t buf, size_t size);
int sys_read(int fd, userptr_t buf, size_t size);
int sys_open(char *path, int oflag);
int sys_write(int fd, userptr_t buf, size_t size, int *err)
{
    struct proc *current_process = curproc;

    int i;
    char *p = (char *)buf;

    if (fd < 0 || fd > OPEN_MAX)
    {
        *err = EBADF;
        return -1;
    }

    if (current_process->process_file_table[fd] == NULL)
    {
        *err = EBADF;
        return -1;
    }
    if (buf == NULL)
    {
        *err = EFAULT;
        return -1;
    }

    if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    {
        kprintf("syscall done - File descriptor error: try stdout o stderror");
        return -1;
    }
    for (i = 0; i < (int)size; i++)
    {
        putch(p[i]);
    }

    return (int)size;
}
int sys_read(int fd, userptr_t buf, size_t size)
{
    int i;
    char *p = (char *)buf;
    if (fd != STDIN_FILENO)
    {
        kprintf("syscall done- File descriptor error: try stdin");
    }
    for (i = 0; i < (int)size; i++)
    {
        p[i] = getch();
        if (p[i] < 0)
            return i;
    }

    return (int)size;
}
int sys_open(char *pathname, int flag, int *err)
{
    a int res, flag_masked, fd;
    struct proc *current_process = curproc;
    size_t size;
    struct vnode *vn;
    char path[PATH_MAX];

    if (pathname == NULL)
    {
        *err = EFAULT;
        return -1;
    }
    res = copyinstr((userptr_t)pathname, path, PATH_MAX, &size);
    if (res)
    {
        *err = res;
        return -1;
    }
    flag_masked = oflag & O_ACCMODE;
    if ((flag_masked != O_RDWR && flag_masked != O_RDONLY && flag_masked != O_WRONLY) || flag > 255)
    {
        *err = EINVAL;
        return -1;
    }
    mode_t mode;
    *err = vfs_open(path, flag, mode, &vn);
    if (*err)
    {
        return -1;
    }
    fd = fd_assign(current_process, vn, flag, err);
    if (fd)
    {
        current_process->process_file_table[fd]->flag = flag;
        return fd;
    }
    else
    {
        return -1;
    }
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
    //get the vn for the vfs_close
    vn = current_process->process_file_table[fd]
}