#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    if(argc != 1)
    {
        fprintf(2, "pingpong called with too many arguments\n");
        exit(1);
    }
    char msg[5]; //holds ping or pong msg
    int child_p[2]; //pipe for child 
    int par_p[2];   //pipe for parent
    
    pipe(child_p);
    pipe(par_p);


    if(fork() ==0) //child process
    {
        //child need to recieve ping and write pong
        // child recieves on par_p[0] and writes on child_p[1]
        // close the other ends of pipes
        close(par_p[1]);
        close(child_p[0]);

        if(read(par_p[0], msg, 5) == 5){
            fprintf(0, " %d :  received  %s\n", getpid(), msg);
            write(child_p[1], "pong", 5);
            exit(0);
        }
        exit(1);
    }
    //if it is parent process
    //parents first writes into par_p[1] and reads from child_p[0]
    //close the other ends
    close(par_p[0]);
    close(child_p[1]);
    write(par_p[1], "ping", 5);
    if(read(child_p[0], msg, 5) == 5){
        fprintf(0, " %d :  received  %s\n", getpid(), msg);
        exit(0);
    }
    exit(1);
    
}