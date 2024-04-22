// #include "types.h"
// #include "defs.h"
// #include "param.h"
// #include "spinlock.h"
// #include "sleeplock.h"
// #include "mmu.h"
// #include "memlayout.h"
// #include "x86.h"
// #include "fs.h"
// #include "buf.h"
// #include "proc.h"

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
    pte_t *pte_list[NPROC];
    int ref_count;
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
    cprintf("swap_init done\n");
}

void funci(int x)
{
    swap_table[x].ref_count--;
    if(swap_table[x].ref_count<=0)
    {
        swap_table[x].is_free=1;
    }
}

// modified to free the page and also return the freed page
char* swap_page_out() {
    // cprintf("\ninside swap_page_out\n");
    struct proc *victim_proc = find_victim_process();
    cprintf("victim_proc pid: %x\n", victim_proc->pid);
    pte_t* victim_pte = find_victim_pte(victim_proc);
    cprintf("page found\n");
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
    swap_table[i].ref_count = get_ref_count(PTE_ADDR(*victim_pte));

    victim_proc->rss -= PGSIZE;

    // *victim_pte = (*victim_pte & ~0xFFF) | i;
    *victim_pte = (i << 12);

    // *victim_pte &= ~PTE_P;
    *victim_pte |= 0x008;

    for(int smth=0;smth<64;smth++)
    {
        pde_t* entry = get_smthth_entry(V2P(mem_page), smth);
        swap_table[i].pte_list[smth] = entry;
        if(entry!=0)
        {
            *entry=*victim_pte;
        }
    }
    set_ref_count(V2P(mem_page), 0);
    kfree(mem_page);
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
    for(int r=0;r<64;r++)
    {
        copy_out(V2P(mem_page), swap_table[i].pte_list[r],r);
        if(swap_table[i].pte_list[r]!=0)
        {
            *swap_table[i].pte_list[r]=*page_table_entry;
        }
    }
    // *page_table_entry =V2P(mem_page)  | PTE_P | swap_table[i].page_perm;
    
    p->rss += PGSIZE;

    // Free the swap slot
    swap_table[i].is_free = 1;


    // cprintf("after swapping in: *pte =%x\n", *page_table_entry);
    // cprintf("exiting swap_page_in\n\n");
    return 0;
}


void freepage(pte_t* pte)
{
    // int slot_no = *pte >> 12;
    // // cprintf("inside freepage\n");

    // swap_table[slot_no].is_free = 1;
}


void page_fault_handler()
{
  uint faulting_address; // vartual address

  faulting_address = rcr2();


  // Check if the faulting address is in the process's address space
  struct proc *curproc = myproc();
  cprintf(" pid : %d\n",curproc->pid);

//   if (faulting_address >= curproc->sz) {
//     cprintf("Segmentation fault\n");
//     curproc->killed = 1;
//     return;
//   }

  pde_t *pgdir = curproc->pgdir;
  pte_t *pte = walkpgdir(pgdir, (void *)faulting_address, 0);
  if (!pte) {
      // This should not happen
      panic("pte should eeexist");
  }

  if (!(*pte & PTE_P)) {
    swap_page_in(pte, curproc);
    return;
  }

  int r_c = get_ref_count(PTE_ADDR(*pte));
  if (r_c == 1) {
    // If the reference count is 1, then we can just map the page
    *pte = *pte | PTE_W;
    lcr3(V2P(myproc()->pgdir));
    return;
  }

  char *mem = kalloc();
  // if (mem == 0) {
  //   // Out of memory, kill the process
  //   cprintf("page_fault_handler: out of memory\n");
  //   curproc->killed = 1;
  //   lcr3(V2P(pgdir));
  //   return;
  // }

  memmove(mem, (char*)P2V(PTE_ADDR(*pte)), PGSIZE);
  remove_pte(PTE_ADDR(*pte),pte);
  kfree((char*)P2V(PTE_ADDR(*pte)));
  insert_pte(V2P(mem),pte);

  // Update the page table entry to point to the new page
  *pte = V2P(mem) | PTE_FLAGS(*pte);
  *pte |= PTE_W;

  lcr3(V2P(myproc()->pgdir));
  return;

}
