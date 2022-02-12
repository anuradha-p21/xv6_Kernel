#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define MAXPRIME 50

void proc( int *p)
{
    int first;
    close(p[1]); //child will read 
    if(read(p[0], &first, 4)){
        printf("prime  : %d\n", first);
        int cp[2]; 
         if(pipe(cp) == -1)
        {
            fprintf(2, "pipe failed\n");
            exit(1);
        }
        if(fork()) // parent writes into the pipe
        {
            close(cp[0]);
        int next;
        while( read(p[0], &next, 4)){
            if(next % first != 0)
            {
                write(cp[1], &next, 4);
            }
        }
        close(cp[1]);
        wait(0);
        exit(0);
        }
        // child process
        close(p[0]); // closes the previous read
        proc(cp);
        exit(0);
        
    }
    exit(0);
}
int main(int argc, int argv[])
{
    if(argc != 1)
    {
        fprintf(2, "primes called with too many arguments\n");
        exit(1);
    }
    int p[2];
    if(pipe(p) == -1)
    {
         fprintf(2, "pipe failed\n");
        exit(1);
    }
    // the parents writes into pipe
    if(fork()){
    //close reading end
    close(p[0]);
    printf("prime  :  2\n");
        for(int i = 3; i< MAXPRIME; i++)
        {
            if((i & 1) != 0)  // filters out all 2 multiples
            {
                write(p[1], &i, 4) ; // int is 4 bytes
            }
        }
        close(p[1]); // end cur write, close
        wait(0);
        exit(0);
    }
    proc(p);
    exit(0);
}