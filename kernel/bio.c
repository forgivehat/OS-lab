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
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  // struct buf head;
} bcache;

struct bucket {
  struct spinlock lock;
  struct buf head;
} hashtable[NBUCKET];

int
ihash(uint blockno)
{
  return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers

  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++) {
      initsleeplock(&b->lock, "buffer");
  }
  b = bcache.buf;
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&hashtable[i].lock,"bcache.bucket");
    for(int j = 0; j < NBUF / NBUCKET; j++) { 
      b->next = hashtable[i].head.next;
      hashtable[i].head.next = b;
      b++;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  
  int index = ihash(blockno);

  //struct bucket* bucket = hashtable + index;
  acquire(&hashtable[index].lock);

  // Is the block already cached?

  for(b = hashtable[index].head.next; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&hashtable[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  uint min_stamp = (1 << 20);
  struct buf *lru_buf = 0;
  for (b = hashtable[index].head.next; b; b = b->next)
  {
    if (b->refcnt == 0 && b->timestamp < min_stamp)
    {
      lru_buf = b;
      min_stamp = b->timestamp;
    }
  }
  if (lru_buf)
  {
    goto find;
  }
  //没在当前bucket里找到可以替换的buf块时，从其他bucket里找一块
  acquire(&bcache.lock);
  lru_buf = loop_find(lru_buf);
  if (lru_buf)
  {
    lru_buf->next = hashtable[index].head.next;
    hashtable[index].head.next = lru_buf;
    release(&bcache.lock);
    goto find;
  }
  else
  {
    panic("bget: no buffers");
  }

find:
  lru_buf->dev = dev;
  lru_buf->blockno = blockno;
  lru_buf->valid = 0;
  lru_buf->refcnt = 1;
  release(&hashtable[ihash(blockno)].lock);
  acquiresleep(&lru_buf->lock);
  return lru_buf;
}

struct buf*
loop_find(struct buf * lru_buf)
{
  struct buf *b;
  uint min_stamp = 1 << 20;
  for (;;)
  {
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
      if (b->refcnt == 0 && b->timestamp < min_stamp)
      {
        lru_buf = b;
        min_stamp = b->timestamp;
      }
    }
    if(lru_buf) {
      int rbuf_index = ihash(lru_buf->blockno);
      acquire(&hashtable[rbuf_index].lock);
      if(lru_buf->refcnt != 0) {
        release(&hashtable[rbuf_index].lock);
      } else {
      struct buf *pre = &hashtable[rbuf_index].head;
      struct buf *p = hashtable[rbuf_index].head.next;
       while (p != lru_buf) {
      pre = pre->next;
      p = p->next;
    }
      pre->next = p->next;
      release(&hashtable[rbuf_index].lock);
        break;
      }
    }
  }
  return lru_buf;
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
  
  int index = ihash(b->blockno);
  acquire(&hashtable[index].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timestamp = ticks;
  }
  release(&hashtable[index].lock);
}

void
bpin(struct buf *b) {
  acquire(&hashtable[ihash(b->blockno)].lock);
  b->refcnt++;
  release(&hashtable[ihash(b->blockno)].lock);
}

void
bunpin(struct buf *b) {
  acquire(&hashtable[ihash(b->blockno)].lock);
  b->refcnt--;
  release(&hashtable[ihash(b->blockno)].lock);
}


