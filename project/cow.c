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
    // cprintf("page fault\n");
    // Get the faulting address
    uint faulting_address; // vartual address

    faulting_address = rcr2();

    // Check if the faulting address is in the process's address space
    struct proc *curproc = myproc();

    if (faulting_address < curproc->sz) {
        // The address is in the process's address space, so this is a write to a COW page
        // Find the page table entry for the faulting address
        pde_t *pgdir = curproc->pgdir;
        pte_t *pte = walkpgdir(pgdir, (char*)faulting_address, 0);
        if (!pte) {
            // This should not happen
            panic("pte should exist");
        }

        // Check the proc_count for the faulting page
        if (rmap_table[PGNUM(PTE_ADDR(*pte))].proc_count > 1) {
            // There are multiple processes referencing the page
            // Allocate a new page
            char *mem = kalloc();
            if (mem == 0) {
                // Out of memory, kill the process
                cprintf("out of memory\n");
                curproc->killed = 1;
                return;
            }
            // memset(mem, 0, PGSIZE);

            // Copy the contents of the original page to the new page
            memmove(mem, (char*)P2V(PTE_ADDR(*pte)), PGSIZE);

            // Update the page table entry to point to the new page
            *pte = V2P(mem) | PTE_W | PTE_FLAGS(*pte) | PTE_P;
            
            // Decrement the proc_count for the original page
            rmap_table[PGNUM(PTE_ADDR(*pte))].proc_count--;

            // Increment the proc_count for the new page
            rmap_table[PGNUM(V2P(mem))].proc_count++;
        } else {
            // The faulting process is the only one referencing the page
            // Mark the page as writable
            *pte |= PTE_W;
        }
        // Return to let the process continue execution
        lcr3(V2P(pgdir));
        return;
    }

    // The faulting address is not in the process's address space, kill the process
    cprintf("segmentation fault\n");
    // curproc->killed = 1;
}
