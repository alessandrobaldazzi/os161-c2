#include <types.h>
#include <spl.h>
#include <vfs.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <limits.h>
#include <synch.h>
#include <kern/errno.h>
#include "opt-shell.h"

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Table for process to pid conversion.
 */

#if OPT_SHELL //
struct proc *proc_table[PID_MAX];
static volatile unsigned int last = 1;
static volatile unsigned int nproc = 1;
struct lock *pid_lock;

struct openfile system_file_table[SYSTEM_OPEN_MAX];
struct lock *system_table_lock = NULL;
#endif

static struct proc *
proc_create(const char *name)
{
    struct proc *proc;

    proc = kmalloc(sizeof(*proc));
    if (proc == NULL)
    {
        return NULL;
    }
    proc->p_name = kstrdup(name);
    if (proc->p_name == NULL)
    {
        kfree(proc);
        return NULL;
    }

    proc->p_numthreads = 0;
    spinlock_init(&proc->p_lock);

    /* VM fields */
    proc->p_addrspace = NULL;

    /* VFS fields */
    proc->p_cwd = NULL;

    proc->last_fd = 3; //avoiding STDIN,STDOUT,STDERR
    proc->cnt_open = 0;

    proc->cv = cv_create("proc_cv");
    if (proc->cv == NULL)
    {
        kfree(proc);
        return NULL;
    }

    proc->lock = lock_create("proc_lock");
    if (proc->cv == NULL)
    {
        kfree(proc);
        return NULL;
    }

    pid_assign(proc);

    if (proc->pid < 0)
    {
        kfree(proc);
        return NULL;
    }
#if OPT_SHELL
    if (system_table_lock == NULL)
    {
        system_table_lock = lock_create("system_lock");
    }
	proc->exited = 0;
	proc->p_pid = -1;
    bzero(proc->process_file_table, OPEN_MAX * (sizeof(struct process_table *)));
    for (int i = 0; i < 3; i++)
    { //Here, we set the process_file_table for STDIN,STDOUT and STDERR
        proc->process_file_table[i] = kmalloc(sizeof(struct process_table));
        proc->process_file_table[i]->ref = NULL; // pointer to the struct openfile *ref
        proc->process_file_table[i]->offset = 0;
    }
#endif
    return proc;
}
/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */

void proc_destroy(struct proc *proc)
{

    /*
     * You probably want to destroy and null out much of the
     * process (particularly the address space) at exit time if
     * your wait/exit design calls for the process structure to
     * hang around beyond process exit. Some wait/exit designs
     * do, some don't.
     */

    KASSERT(proc != NULL);
    KASSERT(proc != kproc);

    /*
     * We don't take p_lock in here because we must have the only
     * reference to this structure. (Otherwise it would be
     * incorrect to destroy it.)
     */

    /* VFS fields */
    if (proc->p_cwd)
    {
        VOP_DECREF(proc->p_cwd);
        proc->p_cwd = NULL;
    }

    /* VM fields */
    if (proc->p_addrspace)
    {
        /*
         * If p is the current process, remove it safely from
         * p_addrspace before destroying it. This makes sure
         * we don't try to activate the address space while
         * it's being destroyed.
         *
         * Also explicitly deactivate, because setting the
         * address space to NULL won't necessarily do that.
         *
         * (When the address space is NULL, it means the
         * process is kernel-only; in that case it is normally
         * ok if the MMU and MMU- related data structures
         * still refer to the address space of the last
         * process that had one. Then you save work if that
         * process is the next one to run, which isn't
         * uncommon. However, here we're going to destroy the
         * address space, so we need to make sure that nothing
         * in the VM system still refers to it.)
         *
         * The call to as_deactivate() must come after we
         * clear the address space, or a timer interrupt might
         * reactivate the old address space again behind our
         * back.
         *
         * If p is not the current process, still remove it
         * from p_addrspace before destroying it as a
         * precaution. Note that if p is not the current
         * process, in order to be here p must either have
         * never run (e.g. cleaning up after fork failed) or
         * have finished running and exited. It is quite
         * incorrect to destroy the proc structure of some
         * random other process while it's still running...
         */
        struct addrspace *as;

        if (proc == curproc)
        {
            as = proc_setas(NULL);
            as_deactivate();
        }
        else
        {
            as = proc->p_addrspace;
            proc->p_addrspace = NULL;
        }
        as_destroy(as);
    }

    KASSERT(proc->p_numthreads == 0);
    pid_remove(proc);
    spinlock_cleanup(&proc->p_lock);

    cv_destroy(proc->cv);
    lock_destroy(proc->lock);

    kfree(proc->p_name);
    kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void proc_bootstrap(void)
{
    kproc = proc_create("[kernel]");
    if (kproc == NULL)
    {
        panic("proc_create for kproc failed\n");
    }
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
    struct proc *newproc;

    newproc = proc_create(name);
    if (newproc == NULL)
    {
        return NULL;
    }

    /* VM fields */

    newproc->p_addrspace = NULL;

    /* VFS fields */

    /*
     * Lock the current process to copy its current directory.
     * (We don't need to lock the new process, though, as we have
     * the only reference to it.)
     */
    spinlock_acquire(&curproc->p_lock);
    if (curproc->p_cwd != NULL)
    {
        VOP_INCREF(curproc->p_cwd);
        newproc->p_cwd = curproc->p_cwd;
    }
    spinlock_release(&curproc->p_lock);

    return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int proc_addthread(struct proc *proc, struct thread *t)
{
    int spl;

    KASSERT(t->t_proc == NULL);

    spinlock_acquire(&proc->p_lock);
    proc->p_numthreads++;
    spinlock_release(&proc->p_lock);

    spl = splhigh();
    t->t_proc = proc;
    splx(spl);

    return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void proc_remthread(struct thread *t)
{
    struct proc *proc;
    int spl;

    proc = t->t_proc;
    KASSERT(proc != NULL);

    spinlock_acquire(&proc->p_lock);
    KASSERT(proc->p_numthreads > 0);
    proc->p_numthreads--;
    spinlock_release(&proc->p_lock);

    spl = splhigh();
    t->t_proc = NULL;
    splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
    struct addrspace *as;
    struct proc *proc = curproc;

    if (proc == NULL)
    {
        return NULL;
    }

    spinlock_acquire(&proc->p_lock);
    as = proc->p_addrspace;
    spinlock_release(&proc->p_lock);
    return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
    struct addrspace *oldas;
    struct proc *proc = curproc;

    KASSERT(proc != NULL);

    spinlock_acquire(&proc->p_lock);
    oldas = proc->p_addrspace;
    proc->p_addrspace = newas;
    spinlock_release(&proc->p_lock);
    return oldas;
}

#if OPT_SHELL

int proc_wait(struct proc *p){

	int status;

	lock_acquire(p->lock);
	while(p->exited == 0){
		cv_wait(p->cv, p->lock);
	}

	status = p->status;
	lock_release(p->lock);
 	proc_destroy(p);
	return status;	
}

void pid_assign(struct proc *p){
	
	int i = 0;

	KASSERT(numproc<=PID_MAX);

//checking if we are assigning the pid to the kernel, in this case the right pid to assign is 1
	if(kproc==NULL){
		pid_lock = lock_create("pid_lock");
    		p->pid=1;
		proc_table[1] = kproc;
		last=2;
		numproc++;
		return;
	}

	lock_acquire(pid_lock);

        do {

		if(proc_table[last] == NULL){
			p->pid = (pid_t) last;
			proc_table[last] = p;

			last++;
			if(last > PID_MAX)
				last = 2;

			numproc++;	
			lock_release(pid_lock);
			return;

		} else {
			i++;
			last++;
			if(last >= PID_MAX)
				last = 2;
		}
	} while( i < PID_MAX);

	
	lock_release(pid_lock);
	p->pid = -1;
	return;
}

void pid_remove(struct proc *p){

	lock_acquire(pid_lock);
	unsigned int index = (int)p->pid;
	numproc--;
	proc_table[index]=NULL;
//updating last with the just freed entry, so the pid_assign can start searching from a free position and avoid the do-while
	last = index;
  	lock_release(pid_lock);
}

struct proc *get_proc(pid_t index){
	
	unsigned int i = (int)index;

	if(index < 0 || index >= PID_MAX){
		return NULL;
	}

	return proc_table[i];

}

int assing_fd(struct proc *proc, struct vnode *vnode, int oflag, int *err)
{
    struct process_table *processTable;
    int i, j, flag = true;
    int system_index_free = SYSTEM_OPEN_MAX;

    if (proc->cnt_open >= OPEN_MAX)
    {
        *err = EMFILE;
        return -1;
    }

    if (system_count >= SYSTEM_OPEN_MAX)
    {
        *err = ENFILE;
        return -1;
    }

    if (proc->last_file >= OPEN_MAX)
        proc->last_file = 3;

    //we are looking for the first slot available in the process_file_table
    i = proc->last_fd;
    for (i; i < OPEN_MAX && flag; i++)
    {
        if (proc->process_file_table[i] == NULL)
        {
            proc->last_fd = i;
            proc->cnt_open++;
            flag = false;
        }
        if (i == OPEN_MAX - 1)
        {
            i = 3;
        }
    }
    //lock used in order to avoid that 2 process can open different files.
    lock_acquire(system_file_lock);
    for (j = 0; j < SYSTEM_OPEN_MAX; j++)
    {
        if (system_file_table[j].vn == vnode)
        {
            //the first if means that we are checking if the file has been already open. if it's already open, we have to lock the access
            //since another processes can enter e modify the open file struct.
            lock_acquire(system_file_table[j].p_lock);
            system_file_table[j].refcount++;
            lock_release(system_file_table[j].p_lock);
            lock_release(system_file_table);
            //i took the control of the system_file_table[j] and i upgrade the counter inside the openfile struct in order to keep trace of this new process
            //now i set on the process_file_table the pointer to this specific open file struct
            processTable = kmalloc(sizeof(struct process_table));
            processTable->offset = 0;
            processTable->flag = oflag;
            processTable->ref = &system_file_table[j];
            proc->process_file_table[proc->last_fd] = processTable;
            //create the processTable and filled with the value passed. Then it was placed at the position finded before.
            return proc->last_fd;
        }
        else if (system_file_table[j].vn == NULL && j < system_index_free) //system_index_free has to be the minimum of the free index.
        {
            system_index_free = j;
        }
    }
    if (system_file_table > SYSTEM_OPEN_MAX)
    {
        lock_release(system_file_lock);
        return -1;
    }
    //Since the vnode is "new", we have allocate a new struct openfile in the system_file_table
    //Once vnode is associated to an entry in the system file table, we do not have any synchronization problem of another process), so we can release the system_file_lock
    system_file_table[system_index_free].vn = vnode;
    lock_release(system_file_lock);
    system_file_table[system_index_free].refcount = 1;
    processTable = kmalloc(sizeof(struct process_table));
    processTable->offset = 0;
    processTable->flag = oflag;
    proc->process_file_table[proc->last_fd] = processTable;
    return proc->last_fd;
}

int remove_fd(struct proc *p, int fd)
{
    lock_acquire(p->process_file_table[fd]->ref->p_lock);
    if (p->process_file_table[fd]->ref->refcount > 1)
    {
        p->process_file_table[fd]->ref->refcount--;
        lock_release(p->process_file_table[fd]->ref->p_lock);
    }
    else if (p->process_file_table[fd]->ref->refcount == 1)
    {
        p->process_file_table[fd]->ref->refcount = 0;
        p->process_file_table[fd]->ref->vn = NULL;
        lock_release(p->process_file_table[fd]->ref->p_lock);
        lock_destroy(p->process_file_table[fd]->ref->p_lock);
        kfree(p->process_file_table[fd]);
    }
    p->cnt_open--;
    p->last_fd = fd; //here I set the last fd as the last removed in order to be reassigned when "assign_fd" is called.
    p->process_file_table[fd] = NULL;
}
int get_numproc()
{
    int n = numproc;
    return n;
}

#endif

