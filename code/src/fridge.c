#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;  // By default: the parent is the Controller (ID 0)
char my_fifo[128];
int fifo_fd;

// Device State and Registry Parameters
int is_open = 0;           // 0 = closed, 1 = open
long total_open_time = 0;  // Registry: cumulative time left open
int delay_time = 5;        // Registry: time after which it auto-closes (default 5s)
int percentage = 50;       // Registry: fill percentage (0-100%)
double current_temp = 4.0; // Registry: current internal temperature
double thermostat = 4.0;   // Registry: target temperature
time_t open_start_time = 0;// Timestamp of when the device door was opened

void cleanup_and_exit(int sig) {
    printf("\n[Fridge %d] Shutting down...\n", my_id);
    if (is_open && open_start_time > 0) {
        total_open_time += (long)(time(NULL) - open_start_time);
    }    
    close(fifo_fd);
    unlink(my_fifo); // Remove named pipe from filesystem
    exit(0);
}

void update_temperature() {
    if (is_open) {
        if (current_temp < 20.0) {
            current_temp += 0.5;
        }
    } else {
        if (current_temp > thermostat) {
            current_temp -= 0.2;
            if (current_temp < thermostat) {
                current_temp = thermostat;
            }
        } else if (current_temp < thermostat) {
            current_temp += 0.2;
            if (current_temp > thermostat) {
                current_temp = thermostat;
            }
        }
    }
}

void send_response(int requester_id, const char* response_str, int is_override) {
    char target_fifo[128];
    char final_message[MAX_CMD_LEN];

    if (is_override) {
        snprintf(final_message, MAX_CMD_LEN, "OVERRIDE (Manuale): %s", response_str);
    } else {
        strncpy(final_message, response_str, MAX_CMD_LEN);
    }

    if (requester_id == 0 || requester_id == -1) {
        strcpy(target_fifo, CONTROLLER_FIFO);
    } else {
        snprintf(target_fifo, sizeof(target_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, requester_id);
    }

    int target_fd = open(target_fifo, O_WRONLY | O_NONBLOCK);
    if (target_fd != -1) {
        IPC_Message reply;
        reply.sender_id = my_id;
        reply.target_id = (requester_id == -1) ? 0 : requester_id;
        strncpy(reply.command, final_message, MAX_CMD_LEN);
        write(target_fd, &reply, sizeof(IPC_Message));
        close(target_fd);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./fridge <id>\n");
        exit(EXIT_FAILURE);
    }

    my_id = atoi(argv[1]);
    srand(time(NULL) ^ (getpid() << 16));

    snprintf(my_fifo, sizeof(my_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, my_id);
    
    // FIFO for this specific device
    if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo failed");
        exit(EXIT_FAILURE);
    }

    // Termination signals handling
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGINT, cleanup_and_exit);

    printf("[Fridge %d] Ready. Listening on %s\n", my_id, my_fifo);

    // O_RDWR prevents EOF when readers/writers switch cycles
    // O_NONBLOCK ensures we can loop every second to update temp and auto-close countdowns
    fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK);
    if (fifo_fd < 0) {
        perror("open fifo failed");
        exit(EXIT_FAILURE);
    }

    IPC_Message msg;
    while (1) {
        sleep(1); // Clock tick for thermodynamic adjustments and auto-close timer
        update_temperature();

        // Non-blocking auto-close check
        if (is_open && open_start_time > 0) {
            time_t now = time(NULL);
            if ((now - open_start_time) >= delay_time) {
                is_open = 0;
                total_open_time += (long)(now - open_start_time);
                open_start_time = 0;
                printf("[Fridge %d] Auto-closed due to timeout delay\n", my_id);
            }
        }

        if (read(fifo_fd, &msg, sizeof(IPC_Message)) > 0) {
            printf("[Fridge %d] Received command: %s\n", my_id, msg.command);

            // Mandatory latency simulation (1 to 3 seconds) to test concurrency
            sleep((rand() % 3) + 1);

            // Check if the command is a manual interaction (override)
            int is_override = (msg.sender_id == -1);

            // --- COMMAND INTERPRETATION (PARSER) ---

            if (strncmp(msg.command, "switch open on", 14) == 0) {
                if (!is_open) {
                    is_open = 1;
                    open_start_time = time(NULL); // Save the exact opening timestamp
                    send_response(msg.sender_id, "ACK: Fridge door opened successfully", is_override);
                } else {
                    send_response(msg.sender_id, "ACK: Fridge door is already open", is_override);
                }

            } else if (strncmp(msg.command, "switch close on", 15) == 0) {
                if (is_open) {
                    is_open = 0;
                    total_open_time += (long)(time(NULL) - open_start_time);
                    open_start_time = 0;
                    send_response(msg.sender_id, "ACK: Fridge door closed successfully", is_override);
                } else {
                    send_response(msg.sender_id, "ACK: Fridge door is already closed", is_override);
                }
                

            } else if (strncmp(msg.command, "info", 4) == 0) {
                long current_total = total_open_time;
                if (is_open) {
                    current_total += (long)(time(NULL) - open_start_time);
                }
                char info_buffer[MAX_CMD_LEN];
                snprintf(info_buffer, sizeof(info_buffer), 
                         "State: %s | Registry: time=%lds delay=%ds fill=%d%% temp=%.1fC thermo=%.1fC parent=%d", 
                         is_open ? "OPEN" : "CLOSED", current_total, delay_time, percentage, current_temp, thermostat, parent_id);
                send_response(msg.sender_id, info_buffer, is_override);
            
            } else if (strncmp(msg.command, "set_parent ", 11) == 0) {
                sscanf(msg.command, "set_parent %d", &parent_id);
                printf("[Fridge %d] Parent ID updated to %d\n", my_id, parent_id);

            } else if (strncmp(msg.command, "set_perc ", 9) == 0) {
                int val;
                // As per specs, content (perc) can only be modified manually
                if (is_override && sscanf(msg.command, "set_perc %d", &val) == 1 && val >= 0 && val <= 100) {
                    percentage = val;
                    send_response(msg.sender_id, "ACK: Percentage updated successfully", is_override);
                } else {
                    send_response(msg.sender_id, "ERR: Operation denied or invalid range", is_override);
                }

            } else if (strncmp(msg.command, "set_thermo ", 11) == 0) {
                double val;
                // As per specs, target temp (thermostat) can only be modified manually
                if (is_override && sscanf(msg.command, "set_thermo %lf", &val) == 1) {
                    thermostat = val;
                    send_response(msg.sender_id, "ACK: Thermostat updated successfully", is_override);
                } else {
                    send_response(msg.sender_id, "ERR: Operation denied", is_override);
                }

            } else {
                send_response(msg.sender_id, "ERR: Unsupported or invalid command", is_override);
            }
        }
    }

    return 0;
}