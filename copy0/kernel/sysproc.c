#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void)
{
    int n;
    argint(0, &n);
    exit(n);
    return 0; // not reached
}

uint64 sys_getpid(void)
{
    return myproc()->pid;
}

uint64 sys_fork(void)
{
    return fork();
}

uint64 sys_wait(void)
{
    uint64 p;
    argaddr(0, &p);
    return wait(p);
}

uint64 sys_sbrk(void)
{
    uint64 addr;
    int n;

    argint(0, &n);
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64 sys_sleep(void)
{
    int n;
    uint ticks0;

    argint(0, &n);
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n)
    {
        if (killed(myproc()))
        {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

#ifdef LAB_PGTBL
// todo:
int pgaccess_main(uint64 start_va, int num, uint64 bitmask_p)
{
    unsigned int bits = 0;
    for (int i = 0; i < num; i++)
    {
        uint64 va = start_va + PGSIZE * i;
        pte_t *pte_p = walk(myproc()->pagetable, va, 0);
        if ((*pte_p & PTE_V))
        {
            if (*pte_p & PTE_A)
            {
                bits = bits | (1U << i);
            }
            unsigned int mask = ~(PTE_A);
            *pte_p = (*pte_p) & mask;
        }
    }
    return copyout(myproc()->pagetable, bitmask_p, (char *)&bits, sizeof(bits));
}
int sys_pgaccess(void)
{
    // 取参数，buf,bitmask
    //使用walk得到最底层的pte的flag，判断
    //若未访问，则清零
    uint64 va;
    argaddr(0, &va);
    int n;
  argint(1, &n);
    uint64 b_addr;
    argaddr(2, &b_addr);
    if (n > 32)
        return -1;
    return pgaccess_main(va, n, b_addr);
}
#endif

uint64 sys_kill(void)
{
    int pid;

    argint(0, &pid);
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}
