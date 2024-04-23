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

char *bcache_lock_names[] = {
    "bcache_0", "bcache_1", "bcache_2", "bcache_3",  "bcache_4",  "bcache_5",  "bcache_6",
    "bcache_7", "bcache_8", "bcache_9", "bcache_10", "bcache_11", "bcache_12", "bcache_13",
};
//* 降低锁的颗粒度。分为十个桶。每个桶都对应着一个buf双向循环带头结点的队列
#define bcachesize 13
struct bufcache
{
    struct spinlock lock;
    struct buf buf[NBUF / bcachesize];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct buf head; //! 双向+头节点+链表+循环
    int num;
} bcache;

struct bufcache bufcacheS[bcachesize];
int hasbuf_num = 0;
void binit(void)
{
    struct buf *b;

    for (int cachei = 0; cachei < bcachesize; cachei++)
    {

        initlock(&bufcacheS[cachei].lock, bcache_lock_names[cachei]);
        bufcacheS[cachei].head.prev = &bufcacheS[cachei].head;
        bufcacheS[cachei].head.next = &bufcacheS[cachei].head;
        for (b = bufcacheS[cachei].buf; b < bufcacheS[cachei].buf + NBUF / bcachesize; b++) //*这么玩？
        {
            b->next = bufcacheS[cachei].head.next;
            b->prev = &bufcacheS[cachei].head;
            initsleeplock(&b->lock, "buffer");
            bufcacheS[cachei].head.next->prev = b;
            bufcacheS[cachei].head.next = b;
        }
    }
    // Create linked list of buffers
    // bcache.head.prev = &bcache.head;
    // bcache.head.next = &bcache.head;
    // for (b = bcache.buf; b < bcache.buf + NBUF; b++) //*这么玩？
    // {
    //     b->next = bcache.head.next;
    //     b->prev = &bcache.head;
    //     initsleeplock(&b->lock, "buffer");
    //     bcache.head.next->prev = b;
    //     bcache.head.next = b;
    // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
//? 硬盘设备没考虑到内容布局上
static struct buf *bget(uint dev, uint blockno)
{
    /*
获取dev对应的缓存队列的锁。
扫描队列里是否已有
若无，则添加
    */
    struct buf *b;

    // acquire(&bcache.lock);
    // printf("bno %d buckid %d\n\n", blockno, blockno % bcachesize);
    acquire(&bufcacheS[blockno % bcachesize].lock);
    struct bufcache *cur_bcache = &bufcacheS[blockno % bcachesize];
    // Is the block already cached?
    // for (b = bcache.head.next; b != &bcache.head; b = b->next)
    // {
    //     if (b->dev == dev && b->blockno == blockno)
    //     {

    //         b->refcnt++;
    //         release(&bcache.lock);
    //         acquiresleep(&b->lock); //?
    //         return b;
    //     }
    // }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    // for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
    // {
    //     if (b->refcnt == 0)
    //     {

    //         b->dev = dev;
    //         b->blockno = blockno;
    //         b->valid = 0;
    //         b->refcnt = 1;
    //         release(&bcache.lock);
    //         acquiresleep(&b->lock);
    //         return b;
    //     }
    // }

    for (b = cur_bcache->head.next; b != &cur_bcache->head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&cur_bcache->lock);
            acquiresleep(&b->lock); //?
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = cur_bcache->head.prev; b != &cur_bcache->head; b = b->prev)
    {
        if (b->refcnt == 0)
        {
            hasbuf_num++;
            // printf("dev %d blockno %d bcacheid %d bufnum %d\n", b->dev, blockno, blockno % bcachesize, hasbuf_num);
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&cur_bcache->lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    // acquire(&bcache.lock);
    // b->refcnt--;
    // if (b->refcnt == 0)
    // {
    //     // no one is waiting for it.
    //     b->next->prev = b->prev;
    //     b->prev->next = b->next;
    //     b->next = bcache.head.next;
    //     b->prev = &bcache.head;
    //     bcache.head.next->prev = b;
    //     bcache.head.next = b;
    // }
    // release(&bcache.lock);
    // printf("bno %d buckid %d\n\n", b->blockno, b->blockno % bcachesize);
    acquire(&bufcacheS[b->blockno % bcachesize].lock);
    struct bufcache *cur_bcache = &bufcacheS[b->blockno % bcachesize];
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        hasbuf_num--;
        //放到开头
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = cur_bcache->head.next;
        b->prev = &cur_bcache->head;
        cur_bcache->head.next->prev = b;
        cur_bcache->head.next = b;
    }
    release(&cur_bcache->lock);
}

void bpin(struct buf *b)
{
    // acquire(&bcache.lock);
    acquire(&bufcacheS[b->blockno % bcachesize].lock);
    b->refcnt++;
    // release(&bcache.lock);
    release(&bufcacheS[b->blockno % bcachesize].lock);
}

void bunpin(struct buf *b)
{
    acquire(&bufcacheS[b->blockno % bcachesize].lock);
    b->refcnt--;
    // release(&bcache.lock);
    release(&bufcacheS[b->blockno % bcachesize].lock);
}
