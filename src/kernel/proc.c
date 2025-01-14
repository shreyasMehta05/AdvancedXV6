#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
//////////////////////////////////////////////////////////////////////////
int mlfqSlice[_MAXLEVEL_MLFQ_] = {1, 4, 8, 16}; // Time slice for each level in MLFQ
// int __TICKS_BOOST__ = 0;
//////////////////////////////////////////////////////////////////////////

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;



// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}



// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
    for(int i=0;i<32;i++){
      p->syscallCount[i]=0;
    }
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

uint64 pseudoRandom(uint64 seed,int max);
// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->ctime = ticks;
  //////////////////////////////////////////////////////////////////////
  // Sigalarm and sigreturn
  p->alarmticks = 0; // initialize alarmticks
  p->sigretaddr = 0; // initialize sigretaddr
  p->passedticks = 0; // initialize passedticks
  p->trapframebackup = 0; // initialize trapframebackup
  //////////////////////////////////////////////////////////////////////


  // For scheduler LBS
  if(p->parent == 0){
    p->numTickets = 1 ; // default number of tickets
  }
  else{
    p->numTickets = p->parent->numTickets; // inherit the number of tickets from the parent
  }
  #ifdef LBS
  // p->numTickets = pseudoRandom(r_time(0),1000);
  // printf("Process %s initialized with %d tickets\n", p->name, p->numTickets);
  #endif
  p->timeOfArrival = ticks; // time of arrival of the process
  p->timeSlice = 1; // default time slice for the process is 1 tick
  //////////////////////////////////////////////////////////////////////
  // For MLFQ
  p->priorityLevel=0;
  p->slice=_RESET_SLICE_MLFQ_;
  p->entryTime=ticks;
  p->execTime=0;
  //////////////////////////////////////////////////////////////////////



  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  for(int i=0;i<32;i++){
    p->syscallCount[i]=0;
  }

  
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // add child syscounts to parent
  for(int i=0;i<32;i++){
    p->parent->syscallCount[i] += p->syscallCount[i];
  }

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// returns the number of tickets of the process
uint64 getTotalTickets(){
  uint64 totalTickets = 0;
  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    {
      totalTickets += p->numTickets;
    }
    release(&p->lock);
  }
  return totalTickets;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// pseudo random number generator to get the probability of the process
uint64 pseudoRandom(uint64 seed,int max){
  uint64 multiplier = 1664525;
  uint64 increment = 1013904223;
  uint64 modulus = 4294967296;
  seed = (seed * multiplier + increment) % modulus;
  if(seed < 0){
    seed = seed * -1;
  }
  if(max == 0){
    return 0;
  }
  seed = seed % max;
  return seed;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// condition to find the process to run based on the lottery scheduling
int atomicLBSFind(int random,int total,struct proc* p){
  if(random <= total+p->numTickets){ // total < random <= total+p->numTickets
    return 1;
  }
  return 0;
}
// find the process to run based on the lottery scheduling
struct proc *processToRunLBS(uint64 totalTickets, uint64 randomNumber){
  // now we will iterate over the processes and find the process to run
  struct proc* processToRun = 0;
  uint64 total = 0;
  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    { 
      
      if(atomicLBSFind(randomNumber,total,p)){
        processToRun = p;
        release(&p->lock);
        break;
      }
      total += p->numTickets;
    }
    release(&p->lock);
  }
  return processToRun;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
int atomicCheckArrival(uint64 minArrivalTime,struct proc* p){
  if(p->timeOfArrival < minArrivalTime){
    return 1;
  }
  return 0;
}
// find the 2nd tier process to run based on the lottery scheduling
struct proc *checkArrival(uint64 minArrivalTime, struct proc* processToRun){
  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    {
      // check if the process has the same number of tickets and has less arrival time
      if(p->numTickets == processToRun->numTickets && atomicCheckArrival(minArrivalTime,p)){
        minArrivalTime = p->timeOfArrival;
        processToRun = p;
      }
    }
    release(&p->lock);
  }
  return processToRun;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// find the process to run based on the lottery scheduling
struct proc *lotteryScheduling(){
  uint64 totalTickets = getTotalTickets();
  // printf("Total Tickets: %d\n",totalTickets);
  if(totalTickets == 0){
    return 0; // no process to run
  }
  uint64 randomNumber = pseudoRandom(r_time(0),totalTickets)+1; // get the random number
  // now we will iterate over the processes and find the process to run
  struct proc* processToRun = processToRunLBS(totalTickets,randomNumber);
//////////////////////////////////////////////////////////////////////////////////////////
  // 2nd pass to find the process with least arrival time
  if(processToRun == 0){
    return 0; // no process to run
  }
  uint64 minArrivalTime = processToRun->timeOfArrival;
  processToRun = checkArrival(minArrivalTime,processToRun);
  return processToRun;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// Increment the ticks for priority boost
// void incrementTicksBoost(){
//   if(__TICKS_BOOST__ >= _PRIORITY_BOOST_MLFQ_) 
//   {
//     __TICKS_BOOST__ = 0;
//   } 
//   else 
//   {
//     __TICKS_BOOST__++;
//   }
// }
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// found the process to run based on the MLFQ scheduling
void foundProcessToRun(int __FOUND_PROCESS_TO_RUN__,struct proc *processToRun, struct cpu *c){
  if(!__FOUND_PROCESS_TO_RUN__) 
  {
    return;
  }
  
  processToRun->state = RUNNING; // change the state to running
  c->proc = processToRun; // set the process to run
  swtch(&c->context, &processToRun->context); // switch the context

  
  c->proc = 0; // reset the process

  release(&processToRun->lock); // release the lock
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// Priority boost for the MLFQ scheduling
// extern void priorityBoost(struct proc *p){
//   if(__TICKS_BOOST__ < _PRIORITY_BOOST_MLFQ_) 
//   {
//     return;
//   }
//   // time to boost the priority
//   p->priorityLevel = 0;
//   p->slice = _RESET_SLICE_MLFQ_;
//   // p->entryTime = ticks;
//   // p->execTime = 0;
//   return;
// }
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// atomic check fto find the best process to run based on the MLFQ scheduling
int atomicCheckProcessToRunMLFQ(struct proc *p,struct proc *processToRun,uint possiblePriority,uint lowestEntryTime){
  if(p->priorityLevel < possiblePriority || (p->priorityLevel == possiblePriority && p->entryTime < lowestEntryTime))  
  {
    return 1;
  }
  return 0;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  if(1){
    p->timeSlice = p->timeSlice;
  }
  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    #ifdef LBS
    // lottery based scheduling
    p = lotteryScheduling();
    if(p == 0){
      continue;
    }
    acquire(&p->lock);
    if(p->state != RUNNABLE){
      release(&p->lock);
      continue;
    }
    p->state = RUNNING;
    c->proc = p;
    p->timeSlice = 1; // reset the time slice
    swtch(&c->context, &p->context);
    c->proc = 0;
    release(&p->lock);
    #elif defined MLFQ  
      int __FOUND_PROCESS_TO_RUN__ = 0;
      struct proc *processToRun = 0;
      uint lowestEntryTime = _MAXXXXXX_; // 1e9
      uint possiblePriority = _MAXLEVEL_MLFQ_;

      // Increment ticks for priority boost to keep track of the time
      // incrementTicksBoost();
      for(p = proc; p < &proc[NPROC]; p++) 
      {
        acquire(&p->lock);
        if(p->state == RUNNABLE) 
        {
          // Priority boost
          // priorityBoost(p);
          
          // Find the highest priority (lowest number) runnable process
          // If multiple processes have the same priority, choose the one that's been waiting the longest
          if(atomicCheckProcessToRunMLFQ(p,processToRun,possiblePriority,lowestEntryTime)) 
          {
            if(processToRun != 0) 
            {
              // release the previous best process to run
              release(&processToRun->lock);
            }
            processToRun = p;
            possiblePriority = p->priorityLevel;
            lowestEntryTime = p->entryTime;
            __FOUND_PROCESS_TO_RUN__ = 1;
          } 
          else 
          {
            // release the process as its not the best process to run
            release(&p->lock);
          }
          
        } 
        else 
        {
          // release the process as its not runnable
          release(&p->lock);
        }
      }
      // Run the process if one was found
      foundProcessToRun(__FOUND_PROCESS_TO_RUN__,processToRun,c);
      // incrementTicksBoost();
      



      
    #else
    // default RR
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    #endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
        p->entryTime = ticks;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s ctime=%d tickets=%d level=", p->pid, state, p->name, p->ctime, p->numTickets);
    printf("\n");
  }
}

// waitx
int waitx(uint64 addr, uint *wtime, uint *rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

void update_time()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNING)
    {
      p->rtime++;
    }
    release(&p->lock);
  } 
  #ifdef MLFQ
     for (p = proc; p < &proc[NPROC]; p++) {
        if (p->pid >= 3 && p->pid <= 13) 
          printf("%d %d %d\n", ticks, p->pid, p->priorityLevel);
      }
  #endif
}


int getSysCount(int mask){
  int getsysCallId = -1;
  // we need to find the system call id by the bit mask
  for(int i = 0; i < 32; i++){
    if((mask & (1 << i)) != 0){
      getsysCallId = i;
      break;
    }
  }
  
  if(getsysCallId == -1){
    return -1;
  }
  // printf("my calling pid: %d\n",myproc()->pid);
  int countSysCall = myproc()->syscallCount[getsysCallId]; // this is for the system call count in original process
  return countSysCall;
}

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
int sigalarm(uint64 ticks,void (*handler)()){
  struct proc *process = myproc();
  process->sigretaddr = (uint64)handler;
  process->alarmticks = ticks;
  return 0;
}
int sigreturn(void){
  struct proc *process = myproc();
  if(process->trapframebackup == 0){
    return -1;
  }
  process->alarmticks = process->passedticks; // reset the alarmticks
  process->passedticks = 0; // reset the passedticks
  memmove(process->trapframe, process->trapframebackup, sizeof(*process->trapframe)); // restore the trapframe
  return process->trapframebackup->a0; // return the trapframebackup
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
int settickets(int tickets){
  if(tickets < 1){
    printf("Error: myproc() returned NULL\n");
    return -1;
  }
  struct proc *p = myproc();
  printf("Setting tickets for process %d to %d\n", p->pid, tickets);
  // acquire(&p->lock);
  p->numTickets = tickets;
  // release(&p->lock);
  return 0;
}
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////