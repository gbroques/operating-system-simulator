#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/sem.h>

union semun {
  int val;    /* Value for SETVAL */
  struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;  /* Array for GETALL, SETALL */
  struct seminfo *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
};

static int setup_interval_timer(int time);
static int setup_interrupt(void);
static void free_shared_memory(void);
static void free_shared_memory_and_abort(int s);
static void print_help_message(char* executable_name,
                               int max_inital_slaves,
                               char* log_file,
                               int max_run_time,
                               int max_sim_time);
static int is_required_argument(char optopt);
static void print_required_argument_message(char optopt);
static int get_clock_shared_segment_size(void);
static void attach_to_shared_memory(void);
static int initialize_binary_semaphore(int sem_id);
static int allocate_binary_semaphore(key_t key, int sem_flags);
static int deallocate_binary_semaphore(int sem_id);
static void fork_and_exec_children(int num_children);
static void fork_and_exec_child(void);
static void get_shared_memory(void);
static void empty_message(void);

static int clock_segment_id;
static int* clock_shared_memory;
static int message_segment_id;
static int* message_shared_memory;
static int sem_id;

int num_slaves_completed = 0;
const int MAX_SLAVES = 100;
FILE* fp;
const int NANO_SECONDS_PER_SECOND = 1000000000;

void handle_child_termination(int signum) {
  int status;
  pid_t pid = wait(&status);

  fprintf(
    fp,
    "[Master] Child %d is terminating at my time %d:%d because it reached %d:%d in slave\n",
    pid,
    *clock_shared_memory,
    *(clock_shared_memory + 1),
    *(message_shared_memory),
    *(message_shared_memory + 1)
  );

  empty_message();
  fork_and_exec_child();
  num_slaves_completed++;
}

int main(int argc, char* argv[]) {
  int help_flag = 0;
  int max_inital_slaves = 5;
  char* log_file = "oss.out";
  int max_run_time = 20;
  int max_sim_time = 2;
  opterr = 0;
  int c;

  while ((c = getopt(argc, argv, "hs:l:t:m:")) != -1) {
    switch (c) {
      case 'h':
        help_flag = 1;
        break;
      case 's':
        max_inital_slaves = atoi(optarg);
        break;
      case 'l':
        log_file = optarg;
        break;
      case 't':
        max_run_time = atoi(optarg);
        break;
      case 'm':
        max_sim_time = atoi(optarg);
        break;
      case '?':
        if (is_required_argument(optopt)) {
          print_required_argument_message(optopt);
        } else if (isprint(optopt)) {
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        } else {
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        }
        return EXIT_FAILURE;
      default:
        abort();
    }
  }

  if (help_flag) {
    print_help_message(argv[0], max_inital_slaves, log_file, max_run_time, max_sim_time);
    exit(EXIT_SUCCESS);
  }

  if (max_inital_slaves < 1) {
    fprintf(stderr, "Invalid argument for option -s\n");
    exit(EXIT_SUCCESS);
  }

  if (max_run_time < 1) {
    fprintf(stderr, "Invalid argument for option -t\n");
    exit(EXIT_SUCCESS);
  }

  if (max_sim_time < 1) {
    fprintf(stderr, "Invalid argument for option -m\n");
    exit(EXIT_SUCCESS);
  }

  if (setup_interrupt() == -1) {
    perror("Failed to set up handler for SIGPROF");
    return EXIT_FAILURE;
  }

  if (setup_interval_timer(max_run_time) == -1) {
    perror("Faled to set up the ITIMER_PROF interval timer");
    return EXIT_FAILURE;
  }

  fp = fopen(log_file, "w+");

  if (fp == NULL) {
    perror("Failed to open log file");
    exit(EXIT_FAILURE);
  }

  signal(SIGINT, free_shared_memory_and_abort);
  signal(SIGCHLD, handle_child_termination);

  get_shared_memory();

  sem_id = allocate_binary_semaphore(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

  attach_to_shared_memory();

  empty_message(); // Initialize message to 0

  int init_sem_success = initialize_binary_semaphore(sem_id);
  if (init_sem_success == -1) {
    perror("Faled to initilaize binary semaphore");
    return EXIT_FAILURE;
  }

  fork_and_exec_children(max_inital_slaves);

  while (1) {
    if (*(clock_shared_memory + 1) == NANO_SECONDS_PER_SECOND) {
      *clock_shared_memory += 1;
      *(clock_shared_memory + 1) = 0;
    } else {
      *(clock_shared_memory + 1) += 2; // Add 4 for more realistic clock
    }
    if (*clock_shared_memory == max_sim_time || num_slaves_completed == MAX_SLAVES) {
      break;
    }
  }

  free_shared_memory();

  return EXIT_SUCCESS;
}

static int get_clock_shared_segment_size() {
  return 2 * sizeof(int);
}

/**
 * Frees all allocated shared memory
 */
static void free_shared_memory(void) {
  shmdt(clock_shared_memory);
  shmdt(message_shared_memory);
  shmctl(clock_segment_id, IPC_RMID, 0);
  shmctl(message_segment_id, IPC_RMID, 0);

  int deallocate_sem_success = deallocate_binary_semaphore(sem_id);
  if (deallocate_sem_success == -1) {
    perror("Failed to deallocate binary semaphore");
    exit(EXIT_FAILURE);
  }
}


/**
 * Free shared memory and abort program
 */
static void free_shared_memory_and_abort(int s) {
  free_shared_memory();
  abort();
}


/**
 * Set up the interrupt handler
 */
static int setup_interrupt(void) {
  struct sigaction act;
  act.sa_handler = free_shared_memory_and_abort;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

/**
 * Sets up an interval timer
 *
 * @param time The duration of the timer
 * @return Zero on success. -1 on error.
 */
static int setup_interval_timer(int time) {
  struct itimerval value;
  value.it_interval.tv_sec = time;
  value.it_interval.tv_usec = 0;
  value.it_value = value.it_interval;
  return (setitimer(ITIMER_PROF, &value, NULL));
}

/**
 * Prints a help message.
 * The parameters correspond to program arguments.
 *
 * @param executable_name Name of the executable
 * @param max_inital_slaves Number of maximum slaves
 * @param log_file Name of the log file
 * @param max_run_time Maximum time program runs
 * @param max_sim_time Maximum simulated time program runs
 */
static void print_help_message(char* executable_name,
                               int max_inital_slaves,
                               char* log_file,
                               int max_run_time,
                               int max_sim_time) {
  printf("Operating System Simulator\n\n");
  printf("Usage: ./%s\n\n", executable_name);
  printf("Arguments:\n");
  printf(" -h  Show help.\n");
  printf(" -s  The maximum number of slave processes spawned. Defaults to %d.\n", max_inital_slaves);
  printf(" -l  Specify the log file. Defaults to '%s'.\n", log_file);
  printf(" -t  Time in seconds master will terminate itself and all children. Defaults to %d.\n", max_run_time);
  printf(" -m  Simulated time in seconds master will terminate itself and all children. Defaults to %d.\n", max_sim_time);
}

/**
 * Determiens whether optopt is a reguired argument.
 *
 * @param optopt Value returned by getopt when there's a missing option argument.
 * @return 1 when optopt is a required argument and 0 otherwise.
 */
static int is_required_argument(char optopt) {
  switch (optopt) {
    case 's':
    case 'l':
    case 't':
    case 'm':
      return 1;
    default:
      return 0;
  }
}

/**
 * Prints a message for a missing option argument.
 * 
 * @param option The option that was missing a required argument.
 */
static void print_required_argument_message(char option) {
  switch (option) {
    case 's':
      fprintf(stderr, "Option -%c requires the number of slave processes.\n", optopt);
      break;
    case 'l':
      fprintf(stderr, "Option -%c requires the name of the log file.\n", optopt);
      break;
    case 't':
      fprintf(stderr, "Option -%c requires the maximum time before master will terminate itself and all its children.\n", optopt);
      break;
    case 'm':
      fprintf(stderr, "Option -%c requires the maximum simulated time before master will terminate itself and all its children.\n", optopt);
      break;
  }
}

/**
 * Allocates shared memory for a simulated clock and message.
 * Populates clock_segment_id and message_segment_id with
 * shared memory segment IDs.
 */
static void get_shared_memory(void) {
  int shared_segment_size = get_clock_shared_segment_size();
  clock_segment_id = shmget(IPC_PRIVATE, shared_segment_size,
    IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  message_segment_id = shmget(IPC_PRIVATE, shared_segment_size,
    IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

  if (clock_segment_id == -1 || message_segment_id == -1) {
    perror("Failed to get shared memory");
    exit(EXIT_FAILURE);
  }
}

/**
 * Attaches to the clock and message shared memory segments.
 * Populates clock_shared_memory and message_shared_memory
 * with references to their appropriate memory location.
 */
static void attach_to_shared_memory(void) {
  clock_shared_memory = (int*) shmat(clock_segment_id, 0, 0);
  message_shared_memory = (int*) shmat(message_segment_id, 0, 0);

  if (*clock_shared_memory == -1 || *message_shared_memory == -1) {
    perror("Failed to attach to shared memory");
    exit(EXIT_FAILURE);
  }
}

/**
 * Forks and executes children processes.
 *
 * @param num_children The number of children to fork.
 */
static void fork_and_exec_children(int num_children) {
  int i;
  for (i = 0; i < num_children; i++) {
    fork_and_exec_child();
  }
}

static int allocate_binary_semaphore(key_t key, int sem_flags) {
  return semget (key, 1, sem_flags);
} 

static int deallocate_binary_semaphore(int sem_id) {
  union semun ignored_argument;
  return semctl(sem_id, 1, IPC_RMID, ignored_argument);
}

static int initialize_binary_semaphore(int sem_id) {
 union semun argument;
 unsigned short values[1];
 values[0] = 1;
 argument.array = values;
 return semctl(sem_id, 0, SETALL, argument);
}

static void empty_message(void) {
  *message_shared_memory = 0;
  *(message_shared_memory + 1) = 0;
}

static void fork_and_exec_child(void) {
    pid_t pid = fork();

    if (pid == -1) {
      perror("Failed to fork");
      exit(EXIT_FAILURE);
    }

    if (pid == 0) { // Child
      char clock_segment_id_string[12];
      sprintf(clock_segment_id_string, "%d", clock_segment_id);
      char message_segment_id_string[12];
      sprintf(message_segment_id_string, "%d", message_segment_id);
      char sem_id_string[12];
      sprintf(sem_id_string, "%d", sem_id);
      execlp(
        "user",
        "user",
        clock_segment_id_string,
        message_segment_id_string,
        sem_id_string,
        (char*) NULL
      );
      perror("Failed to exec");
      _exit(EXIT_FAILURE);
    }
}