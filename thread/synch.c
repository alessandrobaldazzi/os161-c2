/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(*sem));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }
#if OPT_LOCKS
	
	lock->current = kmalloc(sizeof(*lock->current));
	lock->current = NULL;
	lock->sem = sem_create(lock->lk_name,1);
		
#endif

#if OPT_LOCKS_WITH_SPIN
	lock->current = kmalloc(sizeof(*lock->current));
	lock->current = NULL;
	lock->sem_wchan = wchan_create(lock->lk_name);
	if (lock->sem_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->sem_lock);
	lock->sem_count = 1;
	
	
#endif
        // add stuff here as needed

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed
#if OPT_LOCKS
	KASSERT(lock->current==NULL);
	
	kfree(lock->current);
	sem_destroy(lock->sem);
	
	
#endif

#if OPT_LOCKS_WITH_SPIN
	KASSERT(lock->current==NULL);
	spinlock_cleanup(&lock->sem_lock);
	wchan_destroy(lock->sem_wchan);
      	
	kfree(lock->current);
        
#endif
       kfree(lock->lk_name);
       	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        // Write this
#if OPT_LOCKS
	while(lock->current!=NULL);
	spinlock_acquire(&lock->sem->sem_lock);
	
	lock->current = curthread;
	spinlock_release(&lock->sem->sem_lock);
	P(lock->sem);
	
#endif

#if OPT_LOCKS_WITH_SPIN

//	while(lock->current!=NULL);
	spinlock_acquire(&lock->sem_lock);
//	lock->current = curthread;
	
	KASSERT(curthread->t_in_interrupt == false);
	
//	 while (lock->sem_count == 0) {
	while(lock->current!=NULL){
		wchan_sleep(lock->sem_wchan, &lock->sem_lock);
        }
        KASSERT(lock->sem_count > 0);
	lock->current = curthread;
        lock->sem_count--;
	spinlock_release(&lock->sem_lock);
#endif	
          // suppress warning until code gets written
}

void
lock_release(struct lock *lock)
{
        // Write this
#if OPT_LOCKS
	
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&lock->sem->sem_lock);
		lock->current = NULL;
	spinlock_release(&lock->sem->sem_lock);	
	V(lock->sem);
		
#endif

#if OPT_LOCKS_WITH_SPIN
	
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&lock->sem_lock);
	
	lock->current = NULL;
	
 	lock->sem_count++;
        KASSERT(lock->sem_count > 0);
	wchan_wakeone(lock->sem_wchan, &lock->sem_lock);

	spinlock_release(&lock->sem_lock);
#endif
   	// suppress warning until code gets written
}

bool
lock_do_i_hold(struct lock *lock)
{
	bool flag=true;
        // Write this
#if OPT_LOCKS
	spinlock_acquire(&lock->sem->sem_lock);
		
	
	if(lock->current != curthread){
		flag = false;
	}
	else {
		flag = true;
	}
	spinlock_release(&lock->sem->sem_lock);	
	
#endif

#if OPT_LOCKS_WITH_SPIN
	
	spinlock_acquire(&lock->sem_lock);
	
	if(lock->current != curthread){
		flag = false;
	}
	else {
		flag = true;
	}
	spinlock_release(&lock->sem_lock);
#endif
	
     // suppress warning until code gets written

     return flag;   // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(*cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        // add stuff here as needed
#if OPT_CV_IMPL 
	cv->sem_wchan = wchan_create(cv->cv_name);
	if (cv->sem_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	cv->lock = lock_create (cv->cv_name);
	if(cv->lock == NULL){
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->sem_lock);
#endif
        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        // add stuff here as needed
#if OPT_CV_IMPL 
	KASSERT(cv->lock->current == NULL);
	lock_destroy(cv->lock);
	wchan_destroy(cv->sem_wchan);
	spinlock_cleanup(&cv->sem_lock);
#endif
        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
        // Write this
#if OPT_CV_IMPL 
	
	

	spinlock_acquire(&cv->sem_lock);
	lock_release(lock);
	wchan_sleep(cv->sem_wchan,&cv->sem_lock);
	spinlock_release(&cv->sem_lock);

	lock_acquire(lock);
	
#endif
        
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
#if OPT_CV_IMPL 
	
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->sem_lock);
	wchan_wakeone(cv->sem_wchan,&cv->sem_lock);
	spinlock_release(&cv->sem_lock);
	
	
#endif
        // Write this
	
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
#if OPT_CV_IMPL 
	
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->sem_lock);
	wchan_wakeall(cv->sem_wchan,&cv->sem_lock);
	spinlock_release(&cv->sem_lock);
	
	
#endif
	// Write this
	
}

