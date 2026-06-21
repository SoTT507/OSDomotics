#include "common.h"
#include <time.h> // Required for time tracking functions

int my_id;
int is_open = 0;  // 0 = closed, 1 = open
long total_open_time = 0; // Registry: total time left open
time_t open_start_time = 0; // Timestamp of when the window was opened

char my_fifo[128];
int fifo_fd;

void cleanup_and_exit(int sig) {
  printf("\n[Window %d] Shutting down...\n", my_id);
  
  // If the process terminates while open, update the total open time
  if (is_open && open_start_time > 0) {
    total_open_time += (long)(time(NULL) - open_start_time);
  }
  
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./window <id>\n");
    exit(EXIT_FAILURE);
  }

  my_id = atoi(argv[1]);
  sprintf(my_fifo, "%s%d.fifo", FIFO_PATH_PREFIX, my_id);

  // termination
  signal(SIGTERM, cleanup_and_exit);
  signal(SIGINT, cleanup_and_exit);

  // FIFO for this specific device
  if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }

  printf("[Window %d] Ready. Listening on %s\n", my_id, my_fifo);

  // open FIFO for reading (blocks until a writer connects)
  // using O_RDWR prevents EOF when the writer closes the pipe
  fifo_fd = open(my_fifo, O_RDWR);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  while (1) {
    ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
    if (bytes > 0) {
      printf("[Window %d] Received command: %s\n", my_id, msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      if (strncmp(msg.command, "switch open on", 14) == 0) {
        if (!is_open) {
          is_open = 1;
          open_start_time = time(NULL); // save the exact opening timestamp
          printf("[Window %d] Status changed to OPEN\n", my_id);
        } else {
          printf("[Window %d] Already open\n", my_id);
        }
        // Note on specs: open switch instantly returns to "off" after being triggered
      } else if (strncmp(msg.command, "switch close on", 15) == 0) {
        if (is_open) {
          is_open = 0;
          // Calculate how many seconds it was open and add to total time
          total_open_time += (long)(time(NULL) - open_start_time);
          open_start_time = 0;
          printf("[Window %d] Status changed to CLOSED. Total open time: %lds\n", my_id, total_open_time);
        } else {
          printf("[Window %d] Already closed\n", my_id);
        }
        // Note on specs: close switch instantly returns to "off" after being triggered
      } else if (strncmp(msg.command, "info", 4) == 0) {
        long current_total = total_open_time;
        if (is_open) {
          // If currently open, dynamically add the elapsed partial time
          current_total += (long)(time(NULL) - open_start_time);
        }
        printf("[Window %d] Info requested. State: %s, Total open time: %lds\n", 
               my_id, is_open ? "open" : "closed", current_total);
      }

      // TODO: Send acknowledgment back to Controller via CONTROLLER_FIFO
    }
  }

  return 0;
}