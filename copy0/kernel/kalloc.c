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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmem;
struct cpu_freemem
{
    struct spinlock lock;
    struct run *freelist;
    int memory_num;
};
struct cpu_freemem cpu_memS[NCPU];
char *kmem_lock_names[] = {
    "kmem_cpu_0", "kmem_cpu_1", "kmem_cpu_2", "kmem_cpu_3", "kmem_cpu_4", "kmem_cpu_5", "kmem_cpu_6", "kmem_cpu_7",
};
void kinit()
{
    // initlock(&kmem.lock, "kmem");
    // freerange(end, (void *)PHYSTOP);
    //*让一个CPU全部沾满，其他没内存的CPU再偷1000个page
    for (int i = 0; i < NCPU; i++)
    {
        // cpu_memS[i].memory_num = 0;

        initlock(&cpu_memS[i].lock, kmem_lock_names[i]);
        cpu_memS[i].freelist = 0;
        cpu_memS[i].memory_num = 0;
    }
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}
void steal();
void *kalloc_cpu(int cpu);

void kfree_nolock(void *pa, int id)
{
    /*
获锁。
修改memlist
*/

    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    // acquire(&kmem.lock);
    // r->next = kmem.freelist;
    // kmem.freelist = r;
    // release(&kmem.lock);

    struct cpu_freemem *cur_mem = &cpu_memS[id];

    r->next = cur_mem->freelist;
    cur_mem->freelist = r;
    cur_mem->memory_num++;
}
// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
    push_off();
    int id = cpuid();
    struct cpu_freemem *cur_mem = &cpu_memS[id];
    acquire(&cur_mem->lock);
    kfree_nolock(pa, id);
    release(&cur_mem->lock);
    pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void)
{
    /*
获取当前memlist的锁
如果为空，steal，判断是否还为空，
修改memlist
    */
    // struct run *r;

    // acquire(&kmem.lock);
    // r = kmem.freelist;
    // if (r)
    //     kmem.freelist = r->next;
    // release(&kmem.lock);

    // if (r)
    //     memset((char *)r, 5, PGSIZE); // fill with junk
    // return (void *)r;
    push_off();
    int id = cpuid();
    acquire(&cpu_memS[id].lock);
    void *r = kalloc_cpu(id);
    release(&cpu_memS[id].lock);
    pop_off();
    return r;
}

void *kalloc_cpu(int cpuid) //*这是一个helper函数，只有当前文件的函数才会调用。把看锁的任务交给调用者
{
    //*释放cpu的memlist的一个page
    /*
获取当前memlist的锁
如果为空，steal，判断是否还为空，
修改memlist
*/
    struct run *r;
    struct cpu_freemem *cur_mem = &cpu_memS[cpuid];

    r = cur_mem->freelist;
    if (!r)
    {
        steal(cpuid); //调用需要锁的kfree
        r = cur_mem->freelist;
    }
    if (r)
    {
        cur_mem->freelist = r->next;
        cur_mem->memory_num--;
        memset((char *)r, 5, PGSIZE); // fill with junk
    }
    return (void *)r;
}

void steal(int curcpu_id)
{
    //* 遍历数组
    int stealnum = 2048;
    for (int cpui = 0; cpui < NCPU && stealnum > 0; cpui++) //! 有可能死锁
    {
        //?为什么不在锁的保护下读num值呢？
        // 首先num==0时要么另一个CPU正在steal，或者为空。
        // 即使CPUi的链表也不为空，可以偷下一个的。
        //极端情况就是所有的cpui链表都在steal或为空
        if (cpui == curcpu_id || cpu_memS[cpui].memory_num == 0)
            continue;
        acquire(&cpu_memS[cpui].lock);
        // printf("cpui %d curcpui %d num %d\n", cpui, curcpu_id, cpu_memS[curcpu_id].memory_num);
        int tnum = cpu_memS[cpui].memory_num;
        if (tnum != 0)
        {
            if (tnum > stealnum)
                tnum = stealnum;
            for (int t = 0; t < tnum; t++) //! bug tnum不减
                kfree_nolock(kalloc_cpu(cpui), curcpu_id);
            stealnum -= tnum;
        }
        // printf("cpui %d curcpui %d num %d\n", cpui, curcpu_id, cpu_memS[curcpu_id].memory_num);
        release(&cpu_memS[cpui].lock);
    }
    // printf("\n");
}
