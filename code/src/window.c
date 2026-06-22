#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;
int is_open = 0; // 0 = closed, 1 = open
char my_fifo[128];
int fifo_fd;

// Registry variables for tracking open-time (Registry: time)
time_t total_open_time = 0;
time_t last_open_time = 0;

void cleanup_and_exit(int sig) {
  (void)sig; // Suppress unused parameter warning
  printf("\n[Window %d] Shutting down...\n", my_id);
  if (is_open) {
    total_open_time += difftime(time(NULL), last_open_time);
  }
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

// support function to send messages to the Controller (Identica a bulb.c)
void send_response(int requester_id, const char* response_str, int is_override) {
  char target_fifo[128];
  char final_message[MAX_CMD_LEN];  

  if (is_override) {
    snprintf(final_message, MAX_CMD_LEN, "OVERRIDE (Manual): %s", response_str);
  } else {
    strncpy(final_message, response_str, MAX_CMD_LEN);
    final_message[MAX_CMD_LEN - 1] = '\0';
  }

  if (requester_id == 0 || requester_id == -1) {
    // respond to controller
    strcpy(target_fifo, CONTROLLER_FIFO);
  } else {
    // respond to logical parent (Hub/Timer) that made the request
    snprintf(target_fifo, sizeof(target_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, requester_id);
  }

  int target_fd = open(target_fifo, O_WRONLY | O_NONBLOCK);
  if (target_fd != -1) {
    IPC_Message response;
    response.sender_id = my_id;
    response.target_id = (requester_id == -1) ? 0 : requester_id; // 0 = Controller
    strncpy(response.command, final_message, MAX_CMD_LEN); // Copia il messaggio finale con OVERRIDE/ACK completi
    
    // Safe write loop
    ssize_t bytes_written = 0;
    char *ptr = (char *)&response;
    while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
      ssize_t w = write(target_fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
      if (w == -1) {
        if (errno == EINTR) continue;
        perror("[Window] Error writing to target FIFO");
        break;
      }
      bytes_written += w;
    }
    close(target_fd);
  } else {
    perror("[Window] Error trying to open target FIFO");
  }
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

  // initializes the random number generator (for the sleep)
  srand(time(NULL) ^ (getpid() << 16));

  // FIFO for this specific device
  if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }

  printf("[Window %d] Ready. Listening on %s\n", my_id, my_fifo);

  fifo_fd = open(my_fifo, O_RDWR);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  while (1) {
    ssize_t total_read = 0;
    char *ptr = (char *)&msg;

    // Safe read loop
    while (total_read < (ssize_t)sizeof(IPC_Message)) {
      ssize_t bytes = read(fifo_fd, ptr + total_read, sizeof(IPC_Message) - total_read);
      if (bytes > 0) {
        total_read += bytes;
      } else if (bytes == 0) {
        break; // EOF
      } else {
        if (errno == EINTR) continue;
        perror("[Window] read error");
        break;
      }
    }

    if (total_read == sizeof(IPC_Message)) {
      printf("[Window %d] Received command: %s\n", my_id, msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      int is_manual_override = (msg.sender_id == -1);

      char action[32];
      char state[32];

      if (sscanf(msg.command, "switch %31s %31s", action, state) == 2) {
        
        // Open button behavior
        if (strcmp(action, "open") == 0) {
          if (strcmp(state, "on") == 0) {
            if (!is_open) {
              is_open = 1;
              last_open_time = time(NULL);
            }
            send_response(msg.sender_id, "ACK: Window turned OPEN (switch 'open' triggered and returned to off)", is_manual_override);
          } else {
            // if "switch close off" is sent, the button is already off
            send_response(msg.sender_id, "ACK: Switch 'open' is already off", is_manual_override);
          }
        } 
        // Close button behavior
        else if (strcmp(action, "close") == 0) {
          if (strcmp(state, "on") == 0) {
            if (is_open) {
              is_open = 0;
              total_open_time += difftime(time(NULL), last_open_time);
            }
            send_response(msg.sender_id, "ACK: Window turned CLOSED (switch 'close' triggered and returned to off)", is_manual_override);
          } else {
            // if "switch close off" is sent, the button is already off
            send_response(msg.sender_id, "ACK: Switch 'close' is already off", is_manual_override);
          }
        } 
        else {
          send_response(msg.sender_id, "ERR: Unsupported switch for window. Use 'open' or 'close'.", is_manual_override);
        }
      }
      
      else if (strncmp(msg.command, "info", 4) == 0) {
        long current_time_on = (long)total_open_time;
        if (is_open) {
          current_time_on += (long)difftime(time(NULL), last_open_time);
        }

        char info_buffer[MAX_CMD_LEN];
        snprintf(info_buffer, sizeof(info_buffer),
                 "INFO: Window ID %d | Status: %s | Total time open: %ld sec",
                 my_id, is_open ? "OPEN" : "CLOSED", current_time_on);
                 
        send_response(msg.sender_id, info_buffer, is_manual_override);
      }

      // Comando SET_PARENT
      else if (strncmp(msg.command, "set_parent ", 11) == 0) {
        sscanf(msg.command, "set_parent %d", &parent_id);
        printf("[Window %d] Parent updated to %d\n", my_id, parent_id);
      }

      else {
        send_response(msg.sender_id, "ERR: Unsupported command", is_manual_override);
      }
    } 
    
    else if (total_read > 0) {
      printf("[Window %d] Discarded partial message (%zd bytes)\n", my_id, total_read);
    } else {
      usleep(100000); 
    }
  }

  return 0;
}