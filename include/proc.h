//
//  proc.h
//
//
//  Created by Francesco Di Franco on 01/12/21.
//

#ifndef proc_h
#define proc_h

#include <stdio.h>
#include <spinlock.h>
#include <types.h>
#include "limits.h"
#include "opt-shell.h"
#define SYSTEM_OPEN_MAX 10 * OPEN_MAX

struct addrspace;
struct thread;
struct vnode;

#if OPT_SHELL

struct openfile
{
    struct vnode *vn;    //vnode of the file
    int refcount;        //used to count how many entries are linked to this openfile.
    struct lock *p_lock; //lock used to realize mutex
};

struct process_table
{
    int offset; //in which point of the file do you wanna start
    int flag;
    struct openfile *ref; //pointer to the openfile struct
};

#endif
struct proc
{
    char *p_name;           /* Name of this process */
    struct spinlock p_lock; /* Lock for this structure */
    unsigned p_numthreads;  /* Number of threads in this process */

    /* VM */
    struct addrspace *p_addrspace; /* virtual address space */

    /* VFS */
    struct vnode *p_cwd; /* current working directory */

    /* add more material here as needed */
#if OPT_SHELL
    struct process_table *process_file_table[OPEN_MAX];
    int last_fd;  //to keep trace of the last index of the process_file_table used of freed
    int cnt_open; //the number of entries of the process_file_table used

    int status; //the exit status of the process
    pid_t pid;
	pid_t p_pid;

    int exited; //is a flag, is 1 if the process has already exited
    struct cv *cv;
    struct lock *lock;
#endif
};

#endif