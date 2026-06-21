#include "common.h"
#include <time.h> // Required for tracking open time, temperature, and auto-close delay

int my_id;
int is_open = 0;    // 0 = closed, 1 = open

long total_open_time = 0;   // Cumulative time left open
int delay_time = 5; // Default auto-close delay in seconds (can be customized)
int percentage = 50;    // Fill percentage (0-100%), modified manually only
double current_temp = 4.0;  // Current internal temperature in Celsius
double thermostat = 4.0;    // Target temperature in Celsius, modified manually only
time_t open_start_time = 0; // Timestamp of when the fridge was opened

char my_fifo[128];
int fifo_fd;

void cleanup_and_exit(int sig) {
  printf("\n[Fridge %d] Shutting down...\n", my_id);
  
  // If the process terminates while open, update the total open time
  if (is_open && open_start_time > 0) {
    total_open_time += (long)(time(NULL) - open_start_time);
  }
  
  close(fifo_fd);
  unlink(my_fifo);  // Remove named pipe from filesystem 
  exit(0);
}

void update_temperature() {
  if (is_open) {
    // Temperature rises when the door is open
    if (current_temp < 20.0) current_temp += 0.5;
  } else {
    // Temperature drops towards the thermostat target when closed
    if (current_temp > thermostat) {
      current_temp -= 0.2;
      if (current_temp < thermostat) current_temp = thermostat;
    } else if (current_temp < thermostat) {
      current_temp += 0.2;
      if (current_temp > thermostat) current_temp = thermostat;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./fridge <id>\n");
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

  printf("[Fridge %d] Ready. Listening on %s\n", my_id, my_fifo);

  // Open FIFO in non-blocking mode to easily manage the auto-close timer loop
  // using O_RDWR prevents EOF when the writer closes the pipe

  fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  while (1) {
    // Periodically simulate temperature changes and handle auto-close
    sleep(1); 
    update_temperature();

    // auto-close
    if (is_open && open_start_time > 0) {
      time_t now = time(NULL);
      if ((now - open_start_time) >= delay_time) {
        is_open = 0;
        total_open_time += (long)(now - open_start_time);
        open_start_time = 0;
        printf("[Fridge %d] Auto-closed after reaching delay threshold (%ds)\n", my_id, delay_time);
      }
    }

    ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
    if (bytes > 0) {
      printf("[Fridge %d] Received command: %s\n", my_id, msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      if (strncmp(msg.command, "switch open on", 14) == 0) {
        if (!is_open) {
          is_open = 1;
          open_start_time = time(NULL);
          printf("[Fridge %d] Door OPENED\n", my_id);
        } else {
          printf("[Fridge %d] Door already open\n", my_id);
        }
      } else if (strncmp(msg.command, "switch close on", 15) == 0) {
        if (is_open) {
          is_open = 0;
          total_open_time += (long)(time(NULL) - open_start_time);
          open_start_time = 0;
          printf("[Fridge %d] Door CLOSED\n", my_id);
        } else {
          printf("[Fridge %d] Door already closed\n", my_id);
        }
      } else if (strncmp(msg.command, "info", 4) == 0) {
        long current_total = total_open_time;
        if (is_open) {
          current_total += (long)(time(NULL) - open_start_time);
        }
        printf("[Fridge %d] Info: State=%s, OpenTime=%lds, Delay=%ds, Fill=%d%%, Temp=%.1fC, Thermostat=%.1fC\n",
               my_id, is_open ? "open" : "closed", current_total, delay_time, percentage, current_temp, thermostat);
      }
      
      // NOTE: Manual-only parameters (perc, thermostat) will be updated directly 
      // via manual interaction bypassing the Controller, matching the rules.

      // TODO: Send acknowledgment back to Controller via CONTROLLER_FIFO
    }
  }

  return 0;
}