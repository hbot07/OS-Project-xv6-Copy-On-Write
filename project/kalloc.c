// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct pte_list{
  pte_t* arr[NPROC];
};

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;
  int rmap[PHYSTOP>>12]; //store number of references to each page
  struct pte_list pte_list_2D[PHYSTOP>>12]; //store the page table entry of each page
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    kmem.rmap[V2P(p)>>12]=0;
    kfree(p);
    // kmem.num_free_pages+=1;
  }
    
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  dec_ref_count(V2P(v));
  if(get_ref_count(V2P(v))>0)
    return;

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  set_ref_count(V2P(v), 0);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.num_free_pages+=1;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

pde_t* get_smthth_entry(uint pa, int i)
{
  return kmem.pte_list_2D[pa>>12].arr[i];

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;

    set_ref_count(V2P((char*)r),1);
    // make all 64 (=NPROC) entries of the page table entry list NULL
    for(int i=0;i<NPROC;i++)
    {
      kmem.pte_list_2D[V2P((char*)r)>>12].arr[i]=0;
    }
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  if(r) return (char*)r;
  
  {
   cprintf("swap out\n"); 
    char* swapped_out_page_addr = swap_page_out();
    cprintf("swap in\n");
    return kalloc();
    if (swapped_out_page_addr == 0)
    {
      panic("kalloc: out of memory");
    }
    return swapped_out_page_addr;
  }


  
}
uint 
num_of_FreePages(void)
{
  acquire(&kmem.lock);

  uint num_free_pages = kmem.num_free_pages;
  
  release(&kmem.lock);
  
  return num_free_pages;
}


void inc_ref_count(uint pa)
{
  // acquire(&kmem.lock);
  kmem.rmap[pa>>12]+=1;
  // release(&kmem.lock);
}

void dec_ref_count(uint pa)
{
  // acquire(&kmem.lock);
  kmem.rmap[pa>>12]-=1;
  // release(&kmem.lock);
}

void set_ref_count(uint pa, int count)
{
  // acquire(&kmem.lock);
  kmem.rmap[pa>>12]=count;
  // release(&kmem.lock);
}

int get_ref_count(uint pa)
{
  // acquire(&kmem.lock);
  int count = kmem.rmap[pa>>12];
  // release(&kmem.lock);
  return count;
}


void insert_pte(uint pa, pte_t* pte)
{
  // acquire(&kmem.lock);
  for (int i = 0; i < NPROC; i++)
  {
    if(kmem.pte_list_2D[pa>>12].arr[i]==pte)
    {
      return;
    }
  }

  for (int i = 0; i < NPROC; i++)
  {
    if(kmem.pte_list_2D[pa>>12].arr[i]==0)
    {
      kmem.pte_list_2D[pa>>12].arr[i]=pte;
      return;
    }
  }
  panic("insert_pte: No space in pte_list_2D");
  // release(&kmem.lock);
}

void copy_out(uint pa,pde_t* pte,int i)
{
  kmem.pte_list_2D[pa>>12].arr[i]=pte;
}

void remove_pte(uint pa, pte_t* pte)
{
  // acquire(&kmem.lock);
  for (int i = 0; i < NPROC; i++)
  {
    if(kmem.pte_list_2D[pa>>12].arr[i]==pte)
    {
      kmem.pte_list_2D[pa>>12].arr[i]=0;
    }
  }
  // release(&kmem.lock);
}
