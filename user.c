#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  printf("User PID %d\n", getpid());
  return EXIT_SUCCESS;
}