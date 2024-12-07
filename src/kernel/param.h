#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define _MAXLEVEL_MLFQ_     4    // Maximum Level in MLFQ
#define _MAXPROCESS_MLFQ_   64   // Maximum Processes in MLFQ
#define _PRIORITY_BOOST_MLFQ_ 30 // Priority Boost in MLFQ
#define _MAXXXXXX_ 1000000000 // Maximum value for a variable -1e9
#define _RESET_SLICE_MLFQ_ 1// Reset the time slice in MLFQ
extern int mlfqSlice[_MAXLEVEL_MLFQ_];
