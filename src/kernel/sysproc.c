#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
uint64
sys_exit(void)
{
  myproc()->syscallCount[SYS_exit]++;
  int n;
  argint(0, &n);
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  myproc()->syscallCount[SYS_getpid]++;
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  myproc()->syscallCount[SYS_fork]++;
  return fork();
}

uint64
sys_wait(void)
{
  myproc()->syscallCount[SYS_wait]++;
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  myproc()->syscallCount[SYS_sbrk]++;
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  myproc()->syscallCount[SYS_sleep]++;
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

uint64
sys_kill(void)
{
  myproc()->syscallCount[SYS_kill]++;
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  myproc()->syscallCount[SYS_uptime]++;
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_waitx(void)
{
  myproc()->syscallCount[SYS_waitx]++;
  uint64 addr, addr1, addr2;
  uint wtime, rtime;
  argaddr(0, &addr);
  argaddr(1, &addr1); // user virtual memory
  argaddr(2, &addr2);
  int ret = waitx(addr, &wtime, &rtime);
  struct proc *p = myproc();
  if (copyout(p->pagetable, addr1, (char *)&wtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p->pagetable, addr2, (char *)&rtime, sizeof(int)) < 0)
    return -1;
  return ret;
}

uint64 
sys_getSysCount(void){
  myproc()->syscallCount[SYS_getSysCount]++;
  int mask; // this is the mask for the system call
  argint(0, &mask);
  // int pid;
  // argint(1, &pid);
  return getSysCount(mask);
}

// signal handling system calls
uint64
sys_sigalarm(void)
{
  myproc()->syscallCount[SYS_sigalarm]++;
  uint64 ticks;
  uint64 handler;
  argaddr(0, &ticks);
  argaddr(1, &handler);
  if(ticks < 0){
    return -1;
  }
  return sigalarm(ticks, (void (*)())handler);
}
uint64
sys_sigreturn(void)
{
  myproc()->syscallCount[SYS_sigreturn]++;
  return sigreturn();
}


uint64 sys_settickets(void){
  myproc()->syscallCount[SYS_settickets]++;
  int numTickets;
  argint(0, &numTickets);
  return settickets(numTickets);
}