#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;
int is_open = 0; // 0 = closed, 1 = open
char my_fifo[128];
int fifo_fd;

// Variabili per il tracciamento del tempo (Registry: time)
time_t total_open_time = 0;
time_t last_open_time = 0;

// Parametri specifici del Frigorifero
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
  unlink(my_fifo); // Rimuove la pipe dal filesystem
  exit(0);
}

// Funzione di supporto per inviare messaggi (Identica a bulb.c e window.c)
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
    IPC_Message response;
    response.sender_id = my_id;
    response.target_id = (requester_id == -1) ? 0 : requester_id;
    strncpy(response.command, final_message, MAX_CMD_LEN);
    
    // Safe write loop garantito
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

// Aggiornamento simulato della temperatura interna
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

  // NOTA: O_RDWR | O_NONBLOCK ci permette di non bloccarci sulla read se non ci sono comandi,
  // così da poter aggiornare continuamente il tempo e le temperature in background.
  fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  time_t last_tick = time(NULL);

  while (1) {
    // Gestione asincrona del tempo interno (1 secondo reale = 1 tick del sistema)
    time_t now = time(NULL);
    if (difftime(now, last_tick) >= 1.0) {
      last_tick = now;
      update_temperature();

      // Meccanismo di Auto-Chiusura se viene superato il delay_time (5 secondi)
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
        // Se la pipe è temporaneamente vuota usciamo per elaborare i cicli temporali
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

      // Simula la latenza di elaborazione richiesta (1-3 secondi)
      sleep((rand() % 3) + 1);

      int is_manual_override = (msg.sender_id == -1);
      char action[32];
      char state[32];

      // 1. Comando INFO
      if (strstr(msg.command, "info") != NULL) {
        long current_time_on = (long)total_open_time;
        if (is_open) {
          current_time_on += (long)difftime(time(NULL), last_open_time);
        }

        char info_buffer[MAX_CMD_LEN];
        snprintf(info_buffer, sizeof(info_buffer), 
                 "INFO: Fridge ID %d | Status: %s | Temp: %.1fC | Thermo: %.1fC | Fill: %d%% | Total time open: %ld sec", 
                 my_id, is_open ? "OPEN" : "CLOSED", current_temp, thermostat, percentage, current_time_on);
                 
        send_response(msg.sender_id, info_buffer, is_manual_override);
      } 
                    
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
                  send_response(msg.sender_id, "ERR: Unsupported command", is_manual_override);
        }
      } 
      
              // 3. Comando LINK / SET_PARENT
      else if (strstr(msg.command, "set_parent") != NULL) {
        sscanf(msg.command, "set_parent %d", &parent_id);
        printf("[Fridge %d] Parent updated to %d\n", my_id, parent_id);
      }

      else if (strstr(msg.command, "thermo") != NULL) {
        double val;
        if (sscanf(msg.command, "switch thermostat %lf", &val) == 1 || sscanf(msg.command, "thermostat %lf", &val) == 1) {
          thermostat = val;
          send_response(msg.sender_id, "Thermostat updated successfully", is_manual_override);
        }
      }

      else {
        send_response(msg.sender_id, "ERR: Unsupported command", is_manual_override);
      }
    } 
    
    else if (total_read > 0) {
      printf("[Fridge %d] Discarded partial message (%zd bytes)\n", my_id, total_read);
    }
  }

  return 0;
}