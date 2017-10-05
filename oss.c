

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

static int setup_interval_timer(int time);
static int setup_interrupt(void);
static void free_shared_memory(void);
static void free_shared_memory_and_abort(int s);
static void print_help_message(char* executable_name,
                               int max_slaves,
                               char* log_file,
                               int max_run_time,
                               int max_sim_time);
static int is_required_argument(char optopt);
static void print_required_argument_message(char optopt);
static int segment_id;
static char* shared_memory;

int main(int argc, char* argv[]) {
  int help_flag = 0;
  int max_slaves = 5;
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
        max_slaves = atoi(optarg);
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
    print_help_message(argv[0], max_slaves, log_file, max_run_time, max_sim_time);
    exit(EXIT_SUCCESS);
  }

  if (max_slaves < 1) {
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

  signal(SIGINT, free_shared_memory_and_abort);

  free_shared_memory();

  return EXIT_SUCCESS;
}

/**
 * Frees all allocated shared memory
 */
static void free_shared_memory(void) {
  printf("Freeing shared memory\n");
  shmdt(shared_memory);
  shmctl(segment_id, IPC_RMID, 0);
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
 *
 * @return Zero on success. -1 on error.
 */
static int setup_interval_timer(int time) {
  struct itimerval value;
  value.it_interval.tv_sec = time;
  value.it_interval.tv_usec = 0;
  value.it_value = value.it_interval;
  return (setitimer(ITIMER_PROF, &value, NULL));
}

static void print_help_message(char* executable_name,
                               int max_slaves,
                               char* log_file,
                               int max_run_time,
                               int max_sim_time) {
  printf("Operating System Simulator\n\n");
  printf("Usage: ./%s\n\n", executable_name);
  printf("Arguments:\n");
  printf(" -h  Show help.\n");
  printf(" -s  The maximum number of slave processes spawned. Defaults to %d.\n", max_slaves);
  printf(" -l  Specify the log file. Defaults to '%s'.\n", log_file);
  printf(" -t  Time in seconds master will terminate itself and all children. Defaults to %d.\n", max_run_time);
  printf(" -m  Simulated time in seconds master will terminate itself and all children. Defaults to %d.\n", max_sim_time);
}

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

static void print_required_argument_message(char optopt) {
  switch (optopt) {
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