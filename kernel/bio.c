// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock eviction_lock;
  struct spinlock lock[NBUCKET];
  struct buf buckets[NBUCKET];

  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.eviction_lock, "eviction_lock");
  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bucket_lock");
    bcache.buckets[i].next = 0;
  }
  for(b=bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.buckets[0].next;
    bcache.buckets[0].next = b;
    b->refcnt = 0;
    b->timestamp = 0;
  }



}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = blockno % NBUCKET;

  acquire(&bcache.lock[bucket]);
  // 已在哈希桶中
  for(b = bcache.buckets[bucket].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[bucket]);
  // 不在哈希桶中
  acquire(&bcache.eviction_lock);  // 获取驱逐锁，防止多个线程同时进行驱逐
  // 再次检查目标桶，防止在获取 eviction_lock 期间被其他 CPU 加载进来
  acquire(&bcache.lock[bucket]);  
  for(b = bcache.buckets[bucket].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucket]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 如果目标桶没有，开始找空闲的 buffer
  uint min_timestamp = 0xffffffff;
  struct buf *true_buf = 0;
  uint true_bucket = -1;
  for(int i=0; i<NBUCKET; i++) {
    if(i != bucket) acquire(&bcache.lock[i]);
    for(b=bcache.buckets[i].next; b!=0; b=b->next){
      if(b->refcnt == 0 && b->timestamp < min_timestamp) {
        min_timestamp = b->timestamp;
        true_buf = b;
        true_bucket = i;
      }
    }
    if(i != bucket) release(&bcache.lock[i]);
  }
  if(true_buf==0) {
    panic("bget: no buffers");
  }
  // 换桶
  if(true_bucket != bucket){
    acquire(&bcache.lock[true_bucket]);
    struct buf *prev = &bcache.buckets[true_bucket];
    while(prev->next != true_buf) prev = prev->next;
    prev->next = true_buf->next;  // 从原桶中移除
    release(&bcache.lock[true_bucket]);
    
    true_buf->next = bcache.buckets[bucket].next;  // 插入新桶
    bcache.buckets[bucket].next = true_buf;
  }

  // 更新 buffer 元信息
  true_buf->dev = dev;
  true_buf->blockno = blockno;
  true_buf->valid = 0;
  true_buf->refcnt = 1;
  release(&bcache.lock[bucket]);
  release(&bcache.eviction_lock);
  acquiresleep(&true_buf->lock);
  return true_buf;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = b->blockno % NBUCKET;
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks;  // 更新访问时间戳
  }
  release(&bcache.lock[bucket]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock[b->blockno % NBUCKET]);
  b->refcnt++;
  release(&bcache.lock[b->blockno % NBUCKET]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[b->blockno % NBUCKET]);
  b->refcnt--;
  release(&bcache.lock[b->blockno % NBUCKET]);
}


