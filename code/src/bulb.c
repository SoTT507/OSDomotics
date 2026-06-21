#include "common.h"

int my_id;
int is_on = 0; // 0 = off, 1 = on
char my_fifo[128];
int fifo_fd;

// Variabili per tracciare il tempo di accensione (Registry: time)
time_t total_time_on = 0;
time_t last_turn_on_time = 0;

void cleanup_and_exit(int sig) {
  printf("\n[Bulb %d] Shutting down...\n", my_id);
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

// support function to send messages to the Controller
void send_to_controller(const char* response_str) {
    int ctrl_fd = open(CONTROLLER_FIFO, O_WRONLY);
    if (ctrl_fd != -1) {
        IPC_Message response;
        response.sender_id = my_id;
        response.target_id = 0; // 0 = Controller
        strncpy(response.command, response_str, MAX_CMD_LEN);
        
        write(ctrl_fd, &response, sizeof(IPC_Message));
        close(ctrl_fd);
    } else {
        perror("[Bulb] Errore nell'apertura della FIFO del Controller");
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

  // Inizializza il generatore di numeri casuali (per la sleep)
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
    ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
    if (bytes > 0) {
      printf("[Bulb %d] Received command: %s\n", my_id, msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      if (strncmp(msg.command, "switch power on", 15) == 0) {
        if (!is_on) {
            is_on = 1;
            last_turn_on_time = time(NULL);
        }
        printf("[Bulb %d] ACK: Bulb turned on\n", my_id);
        send_to_controller("ACK: Bulb turned ON");

      } else if (strncmp(msg.command, "switch power off", 16) == 0) {
          if (is_on) {
            is_on = 0;
            total_time_on += difftime(time(NULL), last_turn_on_time);
          }
        printf("[Bulb %d] ACK: Bulb turned off\n", my_id);
        send_to_controller("ACK: Bulb turned OFF");
      
      } else if (strncmp(msg.command, "info", 4) == 0) {
        // computes the startup time even if the bulb is currently ON
        long current_time_on = (long)total_time_on;
        if (is_on) {
            current_time_on += (long)difftime(time(NULL), last_turn_on_time);
        }

        char info_buffer[MAX_CMD_LEN];
        snprintf(info_buffer, sizeof(info_buffer), 
                 "INFO: Bulb ID %d | Stato: %s | Tempo totale accensione: %ld sec", 
                 my_id, is_on ? "ON" : "OFF", current_time_on);
                 
        send_to_controller(info_buffer);
      }
      else {
        send_to_controller("ERR: Unsupported command");
      }
    }
  }

  return 0;
}
