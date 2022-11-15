#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <machine/trapframe.h>
#include <synch.h>
#include "opt-A2.h"
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;

#if OPT_A2
  lock_acquire(proc_exit_lock);
  p->dead = true;
  unsigned int number_of_children = array_num(p->child);
  for (unsigned int i = 0; i < number_of_children; i++) {
    struct proc *child_process = (struct proc *) array_get(p->child, i);
    child_process->parent = NULL;
    if (child_process->dead) {
      proc_destroy(child_process);
    }
  }
  p->exitstatus = _MKWAIT_EXIT(exitcode);
  p->exitcode = exitcode;
#endif
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

#if OPT_A2
  if (p->parent != NULL) {
    lock_acquire(p->parent->child_lock);
    if (!p->parent->dead) {
      cv_signal(p->parent->child_cv, p->parent->child_lock);
    }
    lock_release(p->parent->child_lock);
  }
  else
    proc_destroy(p);
  lock_release(proc_exit_lock);
#else
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
#endif
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  *retval = curproc->pid;
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
(void) exitstatus;
#if OPT_A2

  lock_acquire(curproc->proc_lock);
  struct proc *child_process;
  unsigned int number_of_children = array_num(curproc->child);
  for (unsigned int i = 0; i < number_of_children; i++) {
    child_process = (struct proc *) array_get(curproc->child, i);
    if (child_process->pid == pid) {
      array_remove(curproc->child, i);
      break;
    }
  }
  lock_release(curproc->proc_lock);

  lock_acquire(curproc->child_lock);
  while (!child_process->dead) {
    cv_wait(curproc->child_cv, curproc->child_lock);
  }
  lock_release(curproc->child_lock);

  result = copyout((void *)&child_process->exitstatus,status,sizeof(int));
  
  lock_acquire(proc_exit_lock);
  proc_destroy(child_process);
  lock_release(proc_exit_lock);
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
#endif
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

int sys_fork(struct trapframe *tf, int *retval) {

  struct proc *child_process = proc_create_runprogram("One Child");

  lock_acquire(curproc->proc_lock);
  array_add(curproc->child, child_process, NULL);
  lock_release(curproc->proc_lock);

  child_process->parent = curproc;
  child_process->pid = pid_counter;
  as_copy(curproc_getas(), &(child_process->p_addrspace));

  struct trapframe *tf_copy = kmalloc(sizeof(struct trapframe));
  KASSERT(tf_copy != NULL);
  memcpy(tf_copy, tf, sizeof(struct trapframe));
  thread_fork("blablabla", child_process, (void *) &enter_forked_process, tf_copy, 0);

  *retval = child_process->pid;
  return 0;
}


int
sys_execv(char *progname, char **args)
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
  (void) args;
	int len_program_name = strlen(progname) + 1;
	char *progname_p = kmalloc(len_program_name * sizeof(char));
	result = copyin((const_userptr_t)progname, (void *)progname_p, len_program_name);
	if (result) {
		return result;
	}
	
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}

	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
