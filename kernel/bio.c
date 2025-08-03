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

#define HTABLE_SZ 23

struct hash_bucket{
  struct spinlock lock;
  struct buf* head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct hash_bucket Q[HTABLE_SZ];
  // free list of buffers, DLL
  struct buf* freelist;// head of freelist
} bcache;

// insert node at the head of freelist
void insert(struct buf* node, struct buf** freelist){
  if(node==0) panic("empty node"); 
  node->prev = 0;
  node->next = *freelist;
  
  if(*freelist)
    (*freelist)->prev = node;
  
  *freelist = node;
}


void print(){
  int cnt = 0;
  struct buf* c = bcache.freelist;
  while(c){
   ++cnt;
   c = c->next;
  }

  printf("Freelist elements: %d\n", cnt);
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers 
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->refcnt = 0;
    insert(b, &bcache.freelist); 
    initsleeplock(&b->lock, "buffer");
  }

  for(int i=0;i<HTABLE_SZ;i++){
    bcache.Q[i].head = 0;
    initlock(&bcache.Q[i].lock, "bcache.bucket");
  }
  print();
  printf("binit done\n");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int hash = (dev + blockno)%HTABLE_SZ;
  //printf("looking for dev %d blockno %d hash %d\n", dev, blockno, hash);
  // acquire bucket lock to search
  acquire(&bcache.lock);
  acquire(&bcache.Q[hash].lock); 
  struct buf* cur = bcache.Q[hash].head;
  while(cur){
    if(cur->dev == dev && cur->blockno == blockno){
      cur->refcnt++;
      release(&bcache.Q[hash].lock);
      release(&bcache.lock);
      acquiresleep(&cur->lock);
      //printf("Buffer cache has dev %d blkno %d refcnt %d\n", dev, blockno, cur->refcnt);
      return cur;
    }

    cur = cur->next;
  }
  
  //printf("Searching for dev %d blkno %d in freelist\n", dev, blockno);
  // not found in hash bucket, get the head in freelist if there's one
  cur = bcache.freelist;
  if(cur){
    cur->dev = dev;
    cur->blockno = blockno;
    cur->valid = 0;
    cur->refcnt = 1;
    bcache.freelist = cur->next;
    if(bcache.freelist)
     bcache.freelist->prev = 0;
    insert(cur, &bcache.Q[hash].head); 
    release(&bcache.Q[hash].lock);
    release(&bcache.lock);
    acquiresleep(&cur->lock);
    //printf("Found a block in freelist for dev %d blockno %d\n", dev, blockno);
    return cur;
  }
  panic("bget: no buffers");
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

// not thread-safe
void remove(struct buf* node, struct buf** freelist){
 if(node==0 || freelist==0) panic("corrupted pointers passed to remove\n");
 if(node->prev == 0)
   *freelist = node->next;
 else
   node->prev->next = node->next;

 if(node->next){
   node->next->prev = node->prev;
 }

 node->next = 0;
 node->prev = 0;
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  //int d = b->dev;
  //int blk = b->blockno;
  //printf("brelse for dev %d blockno %d refcnt %d\n", b->dev, b->blockno, b->refcnt);
  releasesleep(&b->lock);
  int hash = (b->blockno + b->dev)%HTABLE_SZ;
  acquire(&bcache.lock);
  acquire(&bcache.Q[hash].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    remove(b, &bcache.Q[hash].head);
    insert(b, &bcache.freelist);
  }
  release(&bcache.Q[hash].lock);
  release(&bcache.lock);
  //printf("brelse for dev %d blockno %d done!\n", d, blk);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


