#include "types.h"
#include "user.h"


int main(int argc, char *argv[])
{

    int number = atoi(argv[1]);
    settickets(getpid(),number);
    sleep(number);

    while (1)
    {
    }

    exit(); 
}