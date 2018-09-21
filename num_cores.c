#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
Prints the number of cores which are present in the running machine.
*/
int main() {
    printf("This machine has %li cores.\n", sysconf(_SC_NPROCESSORS_ONLN));
}
