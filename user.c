#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

int* clock_shared_memory;
int* message_shared_memory;

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s clock_segment_id message_segment_id\n", argv[0]);
    return EXIT_FAILURE;
  }

  srand(time(NULL));
  const int clock_segment_id = atoi(argv[1]);
  const int message_segment_id = atoi(argv[2]);

  clock_shared_memory = (int*) shmat(clock_segment_id, 0, 0);
  message_shared_memory = (int*) shmat(message_segment_id, NULL, 0);

  if (*clock_shared_memory < 0 || *message_shared_memory < 0) {
    fprintf(stderr, "PID: %d\n", getpid());
    perror("user shmat");
    return EXIT_FAILURE;
  }

  printf("User PID %d\n", getpid());

  shmdt(clock_shared_memory);
  shmdt(message_shared_memory);
  return EXIT_SUCCESS;
}