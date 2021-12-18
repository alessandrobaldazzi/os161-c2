/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.   
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <mips/trapframe.h>


void sys__exit(int status)
{
  
    struct proc *p = curproc;
	struct thread *cur = curthread;

	proc_remthread(cur);

    lock_acquire(p->lock);
	p->exited = 1;
	p->status = _MKWAIT_EXIT(status); 
	cv_broadcast(p->cv, p->lock);
	lock_release(p->lock);	

	thread_exit();

	panic("thread_exit returned (should not happen)\n");
}


pid_t sys_waitpid(pid_t pid,int *status, int options, int* err){
	
	struct proc *p;
	int retval;

	if(pid < 0 || pid >= PID_MAX){
		*err = ESRCH;
		return -1;
	}

	p=get_proc(pid);
   
	if(p == NULL || p->pid != pid){
		*err = ESRCH;
		return -1;
	}

	if(curproc->pid != p->p_pid){
		*err = ECHILD;
		return -1;
	}
	
	retval = proc_wait(p);
	
	if(curproc->pid > 1 && status != NULL){
		*err = copyout(&retval, (userptr_t) status, sizeof(retval));
		if(*err){
			return -1;
		}
	}

	(void)options;
	return pid;

 }

pid_t sys_getpid(){
	return curproc->pid;
}

#if OPT_SHELL

pid_t sys_getppid(){
	return curproc->p_pid;
}


static void call_enter_forked_process(void *tfv, unsigned long dummy) {
	struct trapframe *tf = (struct trapframe *)tfv;
	(void)dummy;
	enter_forked_process(tf);

	panic("enter_forked_process returned (should not happen)\n");
}


int sys_fork(pid_t *child_pid, struct trapframe *ptf, int* err) {
	struct trapframe *ptf_child;
	struct proc *newpr;
	struct process_table *pt;
	int result;

	KASSERT(curproc != NULL);

	if(get_numproc() >= PID_MAX){
		*err = ENPROC;
		return -1;
	}

	newpr = proc_create_runprogram(curproc->p_name);
	if (newpr == NULL) {
		*err = ENOMEM;
		return -1;
	}

	as_copy(curproc->p_addrspace, &(newpr->p_addrspace));
	if(newpr->p_addrspace == NULL){
		proc_destroy(newpr);
		*err = ENOMEM;
		return -1;
	}

	for(int i = 0; i < OPEN_MAX;i++){
		if(curproc->process_file_table[i] != NULL){
			if(newpr->process_file_table[i] != NULL){
				kfree(newpr->process_file_table[i]);
				newpr->process_file_table[i] = NULL;
			}

			pt = curproc->process_file_table[i];
			newpr->process_file_table[i] = pt;
			if(newpr->process_file_table[i]->ref != NULL){
				lock_acquire(newpr->process_file_table[i]->ref->p_lock);
				VOP_INCREF(newpr->process_file_table[i]->ref->vn);
				newpr->process_file_table[i]->ref->refcount++;
				lock_release(newpr->process_file_table[i]->ref->p_lock);
			}
		}
	}	

	newpr->cnt_open = curproc->cnt_open;
	newpr->last_file = curproc->last_file;
  

	ptf_child = kmalloc(sizeof(struct trapframe));
	if(ptf_child == NULL){
		proc_destroy(newpr);
		*err = ENOMEM;
		return -1;
	}
	memcpy(ptf_child, ptf, sizeof(struct trapframe));
	newpr->p_pid = curproc->pid;

	result = thread_fork(
	curthread->t_name, newpr,
	call_enter_forked_process,
	(void *)ptf_child, (unsigned long)0);

	if (result){
		proc_destroy(newpr);
		kfree(ptf_child);
		*err = ENOMEM;
		return -1;
	}

	*child_pid = newpr->pid;

	return 0;
}


static int u2k_args(int argc, char **args, int *size, char **kargs)
{
	int rem_size = ARG_MAX; //max bytes for all the arguments 
	size_t sz; //current size
	int ret,i,j=-1;
	char next_char;
	int error_ret = 0;
	size_t *len;

	for (i = 0; i < argc; i++) {	
	
		do {
			j++;
			ret = copyin((const_userptr_t) &args[i][j], (void *) &next_char,(size_t) sizeof(char));
			if (ret) {
				error_ret = 1;
			}

		} while (next_char != 0 && j < rem_size - 1 && error_ret ==0);

		if (next_char != 0) {
			return E2BIG;
		}
		sz = j;
		
		
		if (error_ret) {
		 	for (int j = 0; j < i; j++) {
					kfree(kargs[j]);
			}
			return ret;
		}
		rem_size -= (sz + 1);
		size[i] = (int) sz + 1;
		
		kargs[i] = kmalloc((sz+1)*sizeof(char));
   		len = kmalloc(sizeof(int));
		ret = copyinstr((const_userptr_t) args[i], kargs[i], sz+1, len);
		if (ret) {
			kfree(kargs[i]);
			kfree(len);
		return ret;
		}

		kfree(len);

	}

	return 0;
}


static
void
k2u_args(int argc, char **args, userptr_t *uargs, vaddr_t *stack_p, int *size)
{
	
	size_t *len;
	int ret;
//from this pointer on, going down, we are storing the string arguments (char)
	userptr_t arg_p = (userptr_t) (*stack_p - argc*sizeof(userptr_t *) - 4);
//from this pointer on, going up, we are storing the address of the arguments. This address contains the pointer to the first argument
	userptr_t *arg_addr_p = (userptr_t *) (*stack_p - argc*sizeof(userptr_t *) - 4);

	for (int i = 0; i < argc; i++) {
//taking the space for the i-th argument string
		arg_p -= size[i];

//saving the pointer to the argument string
		*arg_addr_p = arg_p;
		len = kmalloc(sizeof(int));

//actual copy of the arguments (char)
		ret = copyoutstr(args[i], arg_p, size[i], len);
		if (ret) {
			kfree(len);
		}

	        kfree(len);

//used to go up to the next pointer
		arg_addr_p++;
	}

	*arg_addr_p = NULL;
//saving in uargs the beginning of the array of user stack arguments 
	*uargs = (userptr_t) (*stack_p - argc*sizeof(int) - 4);

//alligning the stack pointer and returning it
	arg_p -= (int) arg_p % 4;
	*stack_p = (vaddr_t) arg_p;
}


static
void
free_array(int argc, int *size, char **kargs)
{
	for (int i = 0; i < argc; i++) {
		kfree(kargs[i]);
	}

	kfree(size);
	kfree(kargs);
}


int
sys_execv(const char *prog, char **args)
{
	int ret;
	int i = 0;
	char *next_arg;
	char *progname;
	char **kargs;
	int argc;
	size_t *len;
	int *size;
	userptr_t uargs;
	struct vnode *v;
	vaddr_t entrypoint,stackptr;
	struct addrspace *as_old;
	struct addrspace *as_new;
	

	if (prog == NULL || args == NULL) {
		return EFAULT;
	}

	


//copy of the first parameter from user to kernel space
	progname = kmalloc((PATH_MAX+1)*sizeof(char));
   	len = kmalloc(sizeof(int));
	ret = copyinstr((const_userptr_t) prog, progname,PATH_MAX+1, len);
	if (ret) {
		kfree(progname);
		kfree(len);
		return ret;
	}

	kfree(len);

	
	if (strcmp(prog, "") == 0) {
		return EINVAL;
	}
	

//getting the number of arguments and store it in argc
	
	do {
		i++;
		ret = copyin((const_userptr_t) &args[i], (void *) &next_arg, (size_t) sizeof(char *));
		if (ret) {
			return ret;
		}

	} while (next_arg != NULL && i < ARG_MAX); 

	if (next_arg != NULL) {
		return E2BIG;
	}

	argc = i;
	
//copying arguments into kernel space
	kargs = kmalloc(argc*sizeof(char *));
	size = kmalloc(argc*sizeof(int));
	ret = u2k_args(argc, args,size, kargs);
	if (ret) {
		kfree(kargs);
		kfree(size);
		kfree(progname);
		return ret;
	}

//doing the same things as runprogram 
	
	ret = vfs_open(progname, O_RDONLY, 0, &v);
	if (ret) {
		kfree(progname);
		free_array(argc, size, kargs);
		return ret;
	}

	as_old = proc_getas();
	as_new = as_create();
	if (as_new == NULL) {
		vfs_close(v);
		kfree(progname);
		free_array(argc, size, kargs);
		return ENOMEM;
	}

//switching the address space
	as_destroy(as_old);
	proc_setas(NULL);
	as_deactivate();
	proc_setas(as_new);
	as_activate();

	ret = load_elf(v, &entrypoint);
	if (ret) {

		proc_setas(NULL);
		as_deactivate();
		proc_setas(as_old);
		as_activate();
		as_destroy(as_new);
		vfs_close(v);
		kfree(progname);
		free_array(argc, size, kargs);
		return ret;
	}
//taking the user stack pointer of the new address space
	ret = as_define_stack(as_new, &stackptr);
	if (ret) {
		proc_setas(NULL);
		as_deactivate();
		proc_setas(as_old);
		as_activate();
		as_destroy(as_new);
		vfs_close(v);
		kfree(progname);
		free_array(argc, size, kargs);
		return ret;
	}

	vfs_close(v);

//copying the kernel array of arguments into the user stack	
	k2u_args(argc, kargs, &uargs , &stackptr ,size);

	kfree(progname);
	free_array(argc, size, kargs);

	enter_new_process(argc, uargs, NULL, stackptr, entrypoint);
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}


#endif
