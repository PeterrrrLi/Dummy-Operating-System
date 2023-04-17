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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A3.h"

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3
vaddr_t core_map;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
bool cm_switch = false;
paddr_t offset = 0;
unsigned n_frames = 0; 
#endif

void
vm_bootstrap(void)
{
	/* Do nothing. */
	#if OPT_A3
	cm_switch = true;

	paddr_t lo, hi;
	ram_getsize(&lo, &hi);

	n_frames = (hi-lo) / PAGE_SIZE - ROUNDUP(((hi-lo) / PAGE_SIZE * sizeof(int)) / PAGE_SIZE, 8);

	core_map = PADDR_TO_KVADDR(lo);

	offset = ROUNDUP(lo + n_frames*sizeof(int), PAGE_SIZE);
	#endif
}


paddr_t getmem(unsigned long npages) {
	unsigned frame = 0;
	paddr_t addr = 0;
	while (frame < n_frames) { 
		bool kaiguan = false; 

		unsigned page = 0;
		while ((page < n_frames - frame) && (page < npages)) {
			if (*( (int *) (core_map + (frame + page) * sizeof(int)) ) != 0) { 
				kaiguan = true; 
				frame += page; 

				while (*( (int *) (core_map + (1 + frame) * sizeof(int))) != 0) { 
					frame++; 
				} 
				break; 
			}
			page++;
		}

		if (!kaiguan) { 

			unsigned page = 0;
			while (page < npages) { 
				*( (int *) (core_map + (frame + page) * sizeof(int)) ) = page + 1;
				page++;
			}

			addr = offset + frame * PAGE_SIZE;
			break;
		}
		frame++;
	}
	return addr;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	#if OPT_A3
	if (!cm_switch) {
	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	}
	else {
		spinlock_acquire(&coremap_lock);

		addr = getmem(npages);

		spinlock_release(&coremap_lock);
	}
	#else
	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	#endif
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	#if OPT_A3
		spinlock_acquire(&coremap_lock);

		paddr_t pa = KVADDR_TO_PADDR(addr);

		for (int i = (pa - offset) / PAGE_SIZE, acc = 0; * (int *) (core_map + i * sizeof(int)) == acc + 1; i++, acc++) {
			* (int *) (core_map + i * sizeof(int)) = 0;
		}

		spinlock_release(&coremap_lock);
	#else
	(void)addr;
	#endif
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		#if OPT_A3
      		return EFAULT;
		/* We always create pages read-write, so we can't get this */
		#else
		panic("dumbvm: got VM_FAULT_READONLY\n");
		#endif
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	// KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	// KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	// KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	bool code_seg = false; 
	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		// ppppppppppppppppppppp
    	paddr = (as->as_pbase1[(faultaddress - vbase1) / PAGE_SIZE]) * PAGE_SIZE + offset;
		code_seg = true;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		// ppppppppppppppppppppp
		paddr = (as->as_pbase2[(faultaddress - vbase2) / PAGE_SIZE]) * PAGE_SIZE + offset;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		// ppppppppppppppppppppp
		paddr = (as->as_stackpbase[(faultaddress - stackbase) / PAGE_SIZE]) * PAGE_SIZE + offset;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	#if OPT_A3
        if (code_seg && as->kaiguan) elo &= ~TLBLO_DIRTY; 
	#endif
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	#if OPT_A3
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		tlb_random(ehi, elo);
		splx(spl);
		return 0;
	#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
	#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	#if OPT_A3
		as->as_vbase1 = 0;
		as->as_npages1 = 0;
		as->as_vbase2 = 0;
		as->as_npages2 = 0;
		as->as_pbase1 = NULL;
		as->as_pbase2 = NULL;
		as->as_stackpbase = NULL;
		as->kaiguan = false;
	#else
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	#endif
	return as;
}

void
as_destroy(struct addrspace *as)
{
	#if OPT_A3
		for (unsigned int i = 0; i < as->as_npages1; i++) {
			// ppppppppppppppppppppp
			free_kpages(PADDR_TO_KVADDR(as->as_pbase1[i] * PAGE_SIZE + offset));
		}
		for (unsigned int i = 0; i < as->as_npages2; i++) {
			// ppppppppppppppppppppp
			free_kpages(PADDR_TO_KVADDR(as->as_pbase2[i] * PAGE_SIZE + offset));
		}
		for (unsigned int i = 0; i < DUMBVM_STACKPAGES; i++) {
			// ppppppppppppppppppppp
			free_kpages(PADDR_TO_KVADDR(as->as_stackpbase[i] * PAGE_SIZE + offset));
		}
	kfree(as);
	#endif
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

#if OPT_A3
#else
static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
#endif

int
as_prepare_load(struct addrspace *as)
{
	#if OPT_A3
		as->as_pbase1 = kmalloc(as->as_npages1 * sizeof(int));
		as->as_pbase2 = kmalloc(as->as_npages2 * sizeof(int));
		as->as_stackpbase = kmalloc(DUMBVM_STACKPAGES * sizeof(int));

		for (unsigned i = 0; i < as->as_npages1; i++) {
			// ppppppppppppppppppppp
			as->as_pbase1[i] = (getppages(1) - offset) / PAGE_SIZE;
		}
		for (unsigned i = 0; i < as->as_npages2; i++) {
			// ppppppppppppppppppppp
			as->as_pbase2[i] = (getppages(1) - offset) / PAGE_SIZE;
		}
		for (int i = 0; i < DUMBVM_STACKPAGES; i++) {
			// ppppppppppppppppppppp
			as->as_stackpbase[i] = (getppages(1) - offset) / PAGE_SIZE;
		}
	#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}
	
	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

#endif
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	#if OPT_A3
	for (unsigned i = 0; i < old->as_npages1; i++) {
		memmove ((void *) PADDR_TO_KVADDR(new->as_pbase1[i]),
				 (const void *) PADDR_TO_KVADDR(old->as_pbase1[i]), PAGE_SIZE);
	}
	for (unsigned i = 0; i < old->as_npages2; i++) {
		memmove ((void *) PADDR_TO_KVADDR(new->as_pbase2[i]),
				 (const void *) PADDR_TO_KVADDR(old->as_pbase2[i]), PAGE_SIZE);
	}
	for (unsigned i = 0; i < DUMBVM_STACKPAGES; i++) {
		memmove ((void *) PADDR_TO_KVADDR(new->as_stackpbase[i]),
				 (const void *) PADDR_TO_KVADDR(old->as_stackpbase[i]), PAGE_SIZE);
	}
	#else
	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	#endif
	*ret = new;
	return 0;
}
