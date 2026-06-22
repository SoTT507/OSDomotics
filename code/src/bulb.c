#include "common.h"

int my_id;
int parent_id = 0;
int is_on = 0; // 0 = off, 1 = on
char my_fifo[128];
int fifo_fd;

// Registry variables for tracking on-time (Registry: time)
time_t total_time_on = 0;
time_t last_turn_on_time = 0;

void cleanup_and_exit(int sig) {
  (void)sig; // Suppress unused parameter warning
  printf("\n[Bulb %d] Shutting down...\n", my_id);
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

// support function to send messages to the Controller
void send_response(int requester_id, const char* response_str, int is_override) {
  char target_fifo[128];
  char final_message[MAX_CMD_LEN];  

  if (is_override) {
    snprintf(final_message, MAX_CMD_LEN, "OVERRIDE (Manual): %s", response_str);
  } else {
    strncpy(final_message, response_str, MAX_CMD_LEN);
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
        strncpy(response.command, response_str, MAX_CMD_LEN);
        // Safe write loop
        ssize_t bytes_written = 0;
        char *ptr = (char *)&response;
        while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
            ssize_t w = write(target_fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
            if (w == -1) {
                if (errno == EINTR) continue;
                perror("[Bulb] Error writing to target FIFO");
                break;
            }
            bytes_written += w;
        }
        close(target_fd);
    } else {
        perror("[Bulb] Error trying to open target FIFO");
    }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./bulb <id>\n");
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

  printf("[Bulb %d] Ready. Listening on %s\n", my_id, my_fifo);

  // open FIFO for reading (blocks until a writer connects)
  // using O_RDWR prevents EOF when the writer closes the pipe
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
            perror("[Bulb] read error");
            break;
        }
    }


    if (total_read == sizeof(IPC_Message)) {
      printf("[Bulb %d] Received command: %s\n", my_id, msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);


      int is_manual_override = (msg.sender_id == -1);
      char prefix[32];


      if (is_manual_override) {
        strcpy(prefix, "OVERRIDE (Manual)");
      } else {
        strcpy(prefix, "ACK");
      }


      if (strncmp(msg.command, "switch power on", 15) == 0) {
        if (!is_on) {
            is_on = 1;
            last_turn_on_time = time(NULL);
        }
        char response[MAX_CMD_LEN];
        snprintf(response, sizeof(response), "%s: Bulb %d turned ON", prefix, my_id);
        send_response(msg.sender_id, response, is_manual_override);
      } 
      

      else if (strncmp(msg.command, "switch power off", 16) == 0) {
          if (is_on) {
            is_on = 0;
            total_time_on += difftime(time(NULL), last_turn_on_time);
          }
        char response[MAX_CMD_LEN];
        snprintf(response, sizeof(response), "%s: Bulb %d turned OFF", prefix, my_id);
        send_response(msg.sender_id, response, is_manual_override);
      
      } 
      

      else if (strncmp(msg.command, "info", 4) == 0) {
        // computes the startup time even if the bulb is currently ON
        long current_time_on = (long)total_time_on;
        if (is_on) {
            current_time_on += (long)difftime(time(NULL), last_turn_on_time);
        }

        char info_buffer[MAX_CMD_LEN];
        snprintf(info_buffer, sizeof(info_buffer), 
                 "INFO: Bulb ID %d | Status: %s | Total time on: %ld sec", 
                 my_id, is_on ? "ON" : "OFF", current_time_on);
                 
        send_response(msg.sender_id, info_buffer, is_manual_override);
      }


      else if (strncmp(msg.command, "set_parent ", 11) == 0) {
        // Updates the parent_id when recieving the link command
        sscanf(msg.command, "set_parent %d", &parent_id);
        printf("[Bulb %d] Parent updated to %d\n", my_id, parent_id);
      }


      else {
        send_response(msg.sender_id, "ERR: Unsupported command", is_manual_override);
      }
    } 
    
    else if (total_read > 0) {
      printf("[Bulb %d] Discarded partial message (%zd bytes)\n", my_id, total_read);
    } else {
      // Prevent aggressive spin loop if FIFO hangs in an unexpected error state
      usleep(100000); 
    }
  }

  return 0;
}