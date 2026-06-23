#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;
int is_open = 0; // 0 = closed, 1 = open
char my_fifo[128];
int fifo_fd;

// variables for tracking time (Registry: time)
time_t total_open_time = 0;
time_t last_open_time = 0;

// specific parameters for the Fridge
int delay_time = 5;       
int percentage = 50;      
double current_temp = 4.0; 
double thermostat = 4.0;   

void cleanup_and_exit(int sig) {
  (void)sig; // Suppress unused parameter warning
  printf("\n[Fridge %d] Shutting down...\n", my_id);
  if (is_open) {
    total_open_time += difftime(time(NULL), last_open_time);
  }
  close(fifo_fd);
  unlink(my_fifo); // remove the pipe from the filesystem
  exit(0);
}

// support function for sending messages
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
    strcpy(target_fifo, CONTROLLER_FIFO);
  } else {
    snprintf(target_fifo, sizeof(target_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, requester_id);
  }

  int target_fd = open(target_fifo, O_WRONLY | O_NONBLOCK);
  if (target_fd != -1) {
    IPC_Message response = {0};
    response.sender_id = my_id;
    response.target_id = (requester_id == -1) ? 0 : requester_id;
    strncpy(response.command, final_message, MAX_CMD_LEN - 1);
    response.command[MAX_CMD_LEN - 1] = '\0';
    
    // Safe write loop
    ssize_t bytes_written = 0;
    char *ptr = (char *)&response;
    while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
      ssize_t w = write(target_fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
      if (w == -1) {
        if (errno == EINTR) continue;
        perror("[Fridge] Error writing to target FIFO");
        break;
      }
      bytes_written += w;
    }
    close(target_fd);
  } else {
    perror("[Fridge] Error trying to open target FIFO");
  }
}

// simulated update of the internal temperature
void update_temperature() {
  if (is_open) {
    if (current_temp < 20.0) current_temp += 0.5; // Si scalda se aperto
  } else {
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

  signal(SIGTERM, cleanup_and_exit);
  signal(SIGINT, cleanup_and_exit);

  srand(time(NULL) ^ (getpid() << 16));

  if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
    perror("mkfifo failed");
    exit(EXIT_FAILURE);
  }

  printf("[Fridge %d] Ready. Listening on %s\n", my_id, my_fifo);

  // O_RDWR | O_NONBLOCK lets us to not get stuck on the read if there are no commands,
  // thus we can update the time and temperatures in background.
  fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  time_t last_tick = time(NULL);

  while (1) {
    // asyncronous management of the internal time (1 real second = 1 system tick)
    time_t now = time(NULL);
    if (difftime(now, last_tick) >= 1.0) {
      last_tick = now;
      update_temperature();

      // auto-close the door if the delay_time is exceeded (5 seconds)
      if (is_open && difftime(now, last_open_time) >= delay_time) {
        is_open = 0;
        total_open_time += difftime(now, last_open_time);
        printf("[Fridge %d] Auto-closed door due to timeout (delay_time expired)\n", my_id);
      }
    }

    ssize_t total_read = 0;
    char *ptr = (char *)&msg;

    // Safe read loop non bloccante per preservare i tick in background
    while (total_read < (ssize_t)sizeof(IPC_Message)) {
      ssize_t bytes = read(fifo_fd, ptr + total_read, sizeof(IPC_Message) - total_read);
      if (bytes > 0) {
        total_read += bytes;
      } else if (bytes == 0 || (bytes == -1 && errno == EAGAIN)) {
        // if the pipe is temporarily empty we exit to process the temporal cycles
        if (total_read == 0) {
          usleep(10000); // 10ms per evitare l'uso intensivo della CPU
        }
        break; 
      } else {
        if (errno == EINTR) continue;
        perror("[Fridge] read error");
        break;
      }
    }

    if (total_read == sizeof(IPC_Message)) {
      printf("[Fridge %d] Received command: %s\n", my_id, msg.command);

      // simulates the latency of processing
      sleep((rand() % 3) + 1);

      int is_manual_override = (msg.sender_id == -1);
      char action[32];
      char state[32];

      // ------ INFO ------
      if (strstr(msg.command, "info") != NULL) {
        long current_time_on = (long)total_open_time;
        if (is_open) {
          current_time_on += (long)difftime(time(NULL), last_open_time);
        }

        char info_buffer[MAX_CMD_LEN];
        snprintf(info_buffer, sizeof(info_buffer), 
                 "INFO: Fridge ID %d | Status: %s | Temp: %.1fC | Thermo: %.1fC | Fill: %d%% | Delay: %d sec | Total time open: %ld sec", 
                 my_id, is_open ? "OPEN" : "CLOSED", current_temp, thermostat, percentage, delay_time, current_time_on);
                 
        send_response(msg.sender_id, info_buffer, is_manual_override);
      } 

      // ------ SWITCH OPEN/CLOSE ------              
      else if (sscanf(msg.command, "switch %31s %31s", action, state) == 2 && (strcmp(action, "open") == 0)) {
        if (strcmp(state, "on") == 0) {
          if (!is_open) {
            is_open = 1;
            last_open_time = time(NULL);
          }
          char response[MAX_CMD_LEN];
          snprintf(response, sizeof(response), "Fridge %d door turned OPEN", my_id);
          send_response(msg.sender_id, response, is_manual_override);
        
        } else if (strcmp(state, "off") == 0) {      
          if (is_open) {
            is_open = 0;
            total_open_time += difftime(time(NULL), last_open_time);
          }
          char response[MAX_CMD_LEN];
          snprintf(response, sizeof(response), "Fridge %d door turned CLOSED", my_id);
          send_response(msg.sender_id, response, is_manual_override);
        } else {
          char err_msg[MAX_CMD_LEN];
          snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Unsupported state for fridge %d door. Use 'on' or 'off'.", ERR_INVALID_COMMAND, my_id);
          send_response(msg.sender_id, err_msg, is_manual_override);
        }
      } 
      
      // ------ SET PARENT ------
      else if (strstr(msg.command, "set_parent") != NULL) {
        sscanf(msg.command, "set_parent %d", &parent_id);
        printf("[Fridge %d] Parent updated to %d\n", my_id, parent_id);
      }

      // ------ MODIFY THERMOSTAT AND PERC (MANUAL ONLY) ------
      else if (strstr(msg.command, "thermostat") != NULL || strstr(msg.command, "perc") != NULL) {
        if (!is_manual_override) {
          char err_msg[MAX_CMD_LEN];
          snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Thermostat and content (perc) can ONLY be modified manually", ERR_UNSUPPORTED_SWITCH);
          send_response(msg.sender_id, err_msg, is_manual_override);
        } 
        else {
          double temp_val;
          int perc_val;
          
          if (sscanf(msg.command, "switch thermostat %lf", &temp_val) == 1 || sscanf(msg.command, "thermostat %lf", &temp_val) == 1) {
            thermostat = temp_val;
            send_response(msg.sender_id, "Thermostat updated successfully", is_manual_override);
          }
          else if (sscanf(msg.command, "switch perc %d", &perc_val) == 1 || sscanf(msg.command, "perc %d", &perc_val) == 1) {
              if (perc_val >= 0 && perc_val <= 100) {
                percentage = perc_val;
                send_response(msg.sender_id, "Fill percentage updated successfully", is_manual_override);
              } else {
                char err_msg[MAX_CMD_LEN];
                snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Percentage must be between 0 and 100", ERR_INVALID_COMMAND);
                send_response(msg.sender_id, err_msg, is_manual_override);
              }
          } else {
            char err_msg[MAX_CMD_LEN];
            snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Invalid syntax for manual parameter update", ERR_INVALID_COMMAND);
            send_response(msg.sender_id, err_msg, is_manual_override);
          }
        }
      }

      else if (strcmp(msg.command, "del") == 0) {
        cleanup_and_exit(SIGTERM);
      }

      else {
        char err_msg[MAX_CMD_LEN];
        snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Unsupported command for Fridge %d.", ERR_INVALID_COMMAND, my_id);
        send_response(msg.sender_id, err_msg, is_manual_override);
      }
    }
    
    else if (total_read > 0) {
      printf("[Fridge %d] Discarded partial message (%zd bytes)\n", my_id, total_read);
    }
  }

  return 0;
}