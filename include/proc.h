
#ifndef proc_h
#define proc_h

#include <spinlock.h>
#include <types.h>
#include "limits.h"
#include "opt-shell.h"
#define SYSTEM_OPEN_MAX 10 * OPEN_MAX

struct addrspace;
struct thread;
struct vnode;



struct openfile
{
    struct vnode *vn;    //vnode of the file
    int of_pointercount; //used to count how many entries are linked to this openfile.
    struct lock *p_lock; //lock used to realize mutex
};

struct process_table
{
    int offset; //in which point of the file do you wanna start
    int flag;
    struct openfile *of_ref; //pointer to the openfile struct
};

struct proc
{
    char *p_name;           /* Name of this process */
    struct spinlock p_lock; /* Lock for this structure */
    unsigned p_numthreads;  /* Number of threads in this process */
    
    struct process_table *process_file_table[OPEN_MAX];
    int status; //exit status of the process
    int last_fd;  //to keep trace of the last index of the process_file_table used of freed
    int cnt_open; //the number of entries of the process_file_table used

    
    struct cv *cv;
    struct lock *lock;
    int exited; //is a flag, is 1 if the process has already exited
   
    pid_t pid;
    pid_t p_pid; //parent


    /* VM */
    struct addrspace *p_addrspace; /* virtual address space */

    /* VFS */
    struct vnode *p_cwd; /* current working directory */

    /* add more material here as needed */
};
/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);
#if OPT_SHELL
 int proc_wait(struct proc *p);
void pid_assign(struct proc *p);
void pid_remove(struct proc *p);
struct proc *get_proc(pid_t index);
int assign_fd(struct proc *proc, struct vnode *vnode, int oflag, int* err);
int remove_fd(struct proc *p, int index);
int get_numproc(void);
#endif

#endif
