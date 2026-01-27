// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel. defined by kernel.ld.

struct run {
  struct run *next;
};

struct ref {
  struct spinlock lock;
  int count[PHYSTOP/PGSIZE];
} ref;
#define PA2INDEX(pa) (((uint64)(pa)) / PGSIZE)

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  acquire(&ref.lock);
  for(int i=0; i<PHYSTOP/PGSIZE; i++) {
    ref.count[i] = 1;
  }
  release(&ref.lock);

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if(kref_dec(pa)>0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&ref.lock);
    ref.count[PA2INDEX(r)]=1;
    release(&ref.lock);
  }
  return (void*)r;
}

// 增加引用次数，返回引用次数
int kref_inc(void *pa) {
  int c;
  acquire(&ref.lock);
  c = ++ref.count[PA2INDEX(pa)];
  release(&ref.lock);
  return c;
}

// 减少引用次数，返回引用次数
int kref_dec(void *pa) {
  int c;
  acquire(&ref.lock);
  c= --ref.count[PA2INDEX(pa)];
  release(&ref.lock);
  return c;
}

// 获取引用次数，返回引用次数
int kref_get(void *pa) {
  int c;
  acquire(&ref.lock);
  c = ref.count[PA2INDEX(pa)];
  release(&ref.lock);
  return c;
}