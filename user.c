#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/sem.h>
#include <time.h>
#include <ctype.h>

// Simulated clock
struct sim_clock {
  int seconds;
  int nano_seconds;
};
union semun {
  int val;    /* Value for SETVAL */
  struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;  /* Array for GETALL, SETALL */
  struct seminfo *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
};

static int binary_semaphore_wait(int sem_id);
static int binary_semaphore_post(int sem_id);
static struct sim_clock get_end_time(struct sim_clock start, int duration);
static int is_passed_end_time(struct sim_clock end);
static int is_message_empty();

const int NANO_SECONDS_PER_SECOND = 1000000000;

int* clock_shared_memory;
int* message_shared_memory;

void handler(int signal_number) {
  shmdt(clock_shared_memory);
  shmdt(message_shared_memory);
  exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
  prctl(PR_SET_PDEATHSIG, SIGHUP);
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &handler;
  sigaction(SIGHUP, &sa, NULL);

  if (argc != 4) {
    fprintf(stderr, "Usage: %s clock_segment_id message_segment_id semaphore_id\n", argv[0]);
    return EXIT_FAILURE;
  }

  srand(time(NULL));
  const int clock_segment_id = atoi(argv[1]);
  const int message_segment_id = atoi(argv[2]);
  const int sem_id = atoi(argv[3]);

  clock_shared_memory = (int*) shmat(clock_segment_id, 0, 0);
  message_shared_memory = (int*) shmat(message_segment_id, NULL, 0);

  if (*clock_shared_memory < 0 || *message_shared_memory < 0) {
    fprintf(stderr, "PID: %d\n", getpid());
    perror("Failed to attach to shared memory");
    return EXIT_FAILURE;
  }

  struct sim_clock start, end;

  start.seconds = *(clock_shared_memory);
  start.nano_seconds = *(clock_shared_memory + 1);
  int duration = (rand() % 10000000) + 1;
  end = get_end_time(start, duration);

  while (1) {
    binary_semaphore_wait(sem_id);
    if (is_passed_end_time(end) && is_message_empty()) {
      *message_shared_memory = *(clock_shared_memory);
      *(message_shared_memory + 1) = *(clock_shared_memory + 1);
      binary_semaphore_post(sem_id);
      break;
    }
    binary_semaphore_post(sem_id);
  }

  shmdt(clock_shared_memory);
  shmdt(message_shared_memory);
  return EXIT_SUCCESS;
}

/**
 * Wait on a binary semaphore.
 * Block until the semaphore value is positive,
 * then decrement it by 1.
 * 
 * @param sem_id The semaphore's id
 * @return The return value of semop
 */
static int binary_semaphore_wait(int sem_id) {
 struct sembuf operations[1];
 /* Use the first (and only) semaphore. */
 operations[0].sem_num = 0;
 /* Decrement by 1. */
 operations[0].sem_op = -1;
 /* Permit undo'ing. */
 operations[0].sem_flg = SEM_UNDO;
 return semop(sem_id, operations, 1);
}

/**
 * Post to a binary semaphore: increment its value by 1.
 * This returns immediately.
 * 
 * @param sem_id The semaphore's id
 * @return The return value of semop
 */
static int binary_semaphore_post(int sem_id) {
 struct sembuf operations[1];
 /* Use the first (and only) semaphore. */
 operations[0].sem_num = 0;
 /* Increment by 1. */
 operations[0].sem_op = 1;
 /* Permit undo'ing. */
 operations[0].sem_flg = SEM_UNDO;
 return semop(sem_id, operations, 1);
}

static struct sim_clock get_end_time(struct sim_clock start, int duration) {
  struct sim_clock end;
  end.seconds = start.seconds;
  end.nano_seconds = start.nano_seconds + duration;
  if (end.nano_seconds % NANO_SECONDS_PER_SECOND != end.nano_seconds) {
    end.seconds += end.nano_seconds / NANO_SECONDS_PER_SECOND;
    end.nano_seconds %= NANO_SECONDS_PER_SECOND;
  }
  return end;
}

static int is_passed_end_time(struct sim_clock end) {
  if (end.seconds < *clock_shared_memory ||
    (end.seconds == *clock_shared_memory && end.nano_seconds < *(clock_shared_memory + 1))) {
    return 1;
  } else {
    return 0;
  }
}

static int is_message_empty() {
  return *message_shared_memory == 0 && *(message_shared_memory + 1) == 0;  
}