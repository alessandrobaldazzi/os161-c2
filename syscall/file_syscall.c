#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <lib.h>
#include <syscall.h>

int sys_write(int fd, userptr_t buf, size_t size);
int sys_read(int fd, userptr_t buf, size_t size);
int sys_write(int fd, userptr_t buf, size_t size)
{

    int i;
    char *p = (char *)buf;
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
