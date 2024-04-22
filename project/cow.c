#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

void page_fault_handler(void) {
    uint faulting_address; // The virtual address that caused the fault
    faulting_address = rcr2(); // Get the faulting address from CR2

    struct proc *curproc = myproc(); // Get the current process

    // Check if the faulting address is within the process's address space
    if (faulting_address >= curproc->sz) {
        cprintf("segmentation fault\n");
        curproc->killed = 1;
        return; // Exit the handler if address is out of bounds
    }

    pde_t *pgdir = curproc->pgdir;
    pte_t *pte = walkpgdir(pgdir, (char*)faulting_address, 0);
    if (!pte) {
        panic("pte should exist"); // Panic if there is no page table entry
    }

    // Handling Copy-On-Write page faults
    if (*pte & PTE_COW) {
        uint pa = PTE_ADDR(*pte);
        int ref_count = rmap_table[PGNUM(pa)].proc_count;

        if (ref_count > 1) {
            // More than one reference to the page: need to create a new page
            char *mem = kalloc();
            if (mem == 0) {
                cprintf("page_fault_handler: out of memory\n");
                curproc->killed = 1;
                return; // Kill the process if no memory is available
            }
            // Copy the contents of the old page to the new page
            memmove(mem, (char*)P2V(pa), PGSIZE);
            // Update the PTE to the new page with writable permissions
            *pte = V2P(mem) | (PTE_FLAGS(*pte) & ~PTE_COW) | PTE_W | PTE_P;
            // Decrement the original page's reference count
            rmap_table[PGNUM(pa)].proc_count--;
            // Increment the reference count for the new page
            rmap_table[PGNUM(V2P(mem))].proc_count = 1;
        } else {
            // Only one reference: simply make the page writable
            *pte |= PTE_W;
            *pte &= ~PTE_COW; // Clear the COW flag
        }
        lcr3(V2P(pgdir)); // Flush the TLB to ensure the new mapping is used
    } else {
        // Not a COW fault, should not happen as we handle only COW pages here
        cprintf("page fault not handled\n");
        curproc->killed = 1;
    }
}
