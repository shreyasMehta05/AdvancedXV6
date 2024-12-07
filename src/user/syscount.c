#include"kernel/types.h"
#include "kernel/stat.h"
#include"user/user.h"
// #include "kernel/fcntl.h"

// mappping of syscall number to syscall name
char* syscall_names[]={
    "",
    "fork",
    "exit",
    "wait",
    "pipe",
    "read",
    "kill",
    "exec",
    "fstat",
    "chdir",
    "dup",
    "getpid",
    "sbrk",
    "sleep",
    "uptime",
    "open",
    "write",
    "mknod",
    "unlink",
    "link",
    "mkdir",
    "close",
    "waitx",
    "getsyscount",
    "sigalarm",
    "sigreturn",
    "settickets"
};
int main(int argc,char* argv[]){
    if(argc<3){
        fprintf(2,"Usage: syscount <mask> command [args]\n");
        exit(1);
    }
    // check if the mask is valid number
    for(int i=0;i<strlen(argv[1]);i++){
        if(argv[1][i]<'0' || argv[1][i]>'9'){
            fprintf(2,"Invalid mask!\n");
            exit(1);
        }
    }
    int mask=atoi(argv[1]);
    int pid = fork();
    int parent = getSysCount(mask);
    if(pid<0){
        fprintf(2,"fork failed!\n");
        exit(1);
    }
    else if(pid==0){
        exec(argv[2],argv+2);
        fprintf(2,"exec failed!\n");
        exit(1);
    }
    else{
        wait(0);
        int child = getSysCount(mask);
        if(parent<0 || child<0){
            fprintf(2,"count failed!\n");
            exit(1);
        }
        for(int i=0;i<26;i++){
            if(mask&(1<<i)){
                mask=i;
                break;
            }
        }
        printf("PID %d called %s %d times.\n",pid,syscall_names[mask],child-parent);
    }
    exit(0);

}