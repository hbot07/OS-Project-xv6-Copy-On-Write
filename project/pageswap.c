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



struct swap_slot {
    int page_perm;
    int is_free;
    uint starting_block_number;
};

struct swap_slot swap_table[(NSWAP/8)];

void swap_init() {


    for (int i = 0; i < (NSWAP/8); i++) {
        swap_table[i].is_free = 1;
        swap_table[i].page_perm = 0;
        swap_table[i].starting_block_number = 2 + i*8; // make this 'better'?
        // using sb.swapstart + i*8 instead of 2 + i*8
    }
}

// modified to free the page and also return the freed page
char* swap_page_out() {
    // cprintf("\ninside swap_page_out\n");
    struct proc *victim_proc = find_victim_process();
    pte_t* victim_pte = find_victim_pte(victim_proc);
    // if (victim_pte == 0)
    // {
    //     panic("victim_pte is null");     
    // }

    // cprintf("victim_proc pid: %x and rss: %d\n", victim_proc->pid, victim_proc->rss);
    // cprintf("*victim_pte: %x\n", *victim_pte);
    struct buf *b;
    char *mem_page;
    int i;

    // Find a free swap slot
    for (i = 0; i < (NSWAP/8); i++) {
        if (swap_table[i].is_free) {
            swap_table[i].is_free = 0;
            swap_table[i].page_perm = PTE_FLAGS(*victim_pte);
            break;
        }
    }

    // If no free swap slot is found, return -1
    if (i == (NSWAP/8)) {
        panic("swap_page_out: no free swap slot found\n");
        return 0;
    }

    // Write the page to the swap slot
    mem_page = (char*)P2V(PTE_ADDR(*victim_pte));

    // cprintf("mem_page: %x\n", mem_page);

    uint j;
    for (j = 0; j < 8; j++) {
        b = bread(ROOTDEV, swap_table[i].starting_block_number + j);
        memmove(b->data, mem_page + j * BSIZE, BSIZE);
        bwrite(b);
        brelse(b);
    }

    // Update the page table entry
    victim_proc->rss -= PGSIZE;

    // *victim_pte = (*victim_pte & ~0xFFF) | i;
    *victim_pte = (i << 12);

    // *victim_pte &= ~PTE_P;
    *victim_pte |= 0x008;
    // cprintf("in swap out --> swap slot: %x, swap_table[i].page_perm %x\n", i, swap_table[i].page_perm);
    
    // cprintf("after swapping out victim: *pte =%x\n", *victim_pte); 
    // cprintf("Exiting swap_page_out\n\n");
    return mem_page;
}

int swap_page_in(pte_t *page_table_entry, struct proc *p)
{
    // cprintf("inside swap_page_in\n");
    // cprintf("p->pid: %x, p->pgdir: %x, p->rss: %d\n", p->pid, p->pgdir, p->rss);
    // cprintf("*page_table_entry: %x\n", *page_table_entry);
    struct buf *b;
    char *mem_page;
    int i;

    // Get the swap slot index from the page table entry
    i = *page_table_entry >> 12;

    // Allocate a new page in memory
    // cprintf("Calling kalloc inside swap_page_in\n");
    mem_page = kalloc();
    // cprintf("kalloc done, mem_page: %x\n", mem_page);
    if (mem_page == 0) {
        return -1; // Not enough memory
    }

    // Read the page from the swap slot
    for (int j = 0; j < 8; j++) {
        b = bread(1, swap_table[i].starting_block_number + j);
        memmove(mem_page + j * BSIZE, b->data, BSIZE);
        brelse(b);
    }

    // cprintf("in swap in --> swap slot: %x, swap_table[i].page_perm %x\n", i, swap_table[i].page_perm);


    *page_table_entry = V2P(mem_page) | PTE_P | swap_table[i].page_perm;
    *page_table_entry &= ~0x008;
    // *page_table_entry =V2P(mem_page)  | PTE_P | swap_table[i].page_perm;
    
    p->rss += PGSIZE;

    // Free the swap slot
    swap_table[i].is_free = 1;


    // cprintf("after swapping in: *pte =%x\n", *page_table_entry);
    // cprintf("exiting swap_page_in\n\n");
    return 0;
}



void page_fault_handler(void)
{
    // cprintf("inside page_fault_handler\n");
    struct proc *p = myproc();
    uint va = rcr2();
    // cprintf("va: %x\n", va);

    pte_t *pte = walkpgdir(p->pgdir, (void *)va, 0);

    // if (!pte) {
    //     // cprintf("page_fault_handler: pte is null\n");
    //     panic("page_fault_handler");
    //     return;
    // }

    // cprintf("\ncalling swap_page_in\n");
    if (swap_page_in(pte, p) < 0) {
        // cprintf("page_fault_handler: swap_page_in failed\n");
        return;
    }
}

void freepage(pte_t* pte)
{
    int slot_no = *pte >> 12;
    // cprintf("inside freepage\n");

    swap_table[slot_no].is_free = 1;
}
