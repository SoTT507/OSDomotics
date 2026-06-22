#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;  // By default: the parent is the Controller (ID 0)
char my_fifo[128];
int fifo_fd;

// Device State and Registry Parameters
int is_on = 0;             // 0 = off, 1 = on
long total_on_time = 0;    // Registry: cumulative time left active
time_t on_start_time = 0;  // Timestamp of when the device was turned on

void cleanup_and_exit(int sig) {
    printf("\n[Bulb %d] Shutting down...\n", my_id);
    
    // If the process terminates while active, update the total active time
    if (is_on && on_start_time > 0) {
        total_on_time += (long)(time(NULL) - on_start_time);
    }
    
    close(fifo_fd);
    unlink(my_fifo); // Remove named pipe from filesystem
    exit(0);
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
        fprintf(stderr, "Usage: ./bulb <id>\n");
        exit(EXIT_FAILURE);
    }

    my_id = atoi(argv[1]);
    snprintf(my_fifo, sizeof(my_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, my_id);
    
    // FIFO for this specific device
    if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo failed");
        exit(EXIT_FAILURE);
    }

    // Termination signals handling
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGINT, cleanup_and_exit);

    printf("[Bulb %d] Ready. Listening on %s\n", my_id, my_fifo);

    // O_RDWR prevents EOF when readers/writers switch cycles
    fifo_fd = open(my_fifo, O_RDWR);
    if (fifo_fd < 0) {
        perror("open fifo failed");
        exit(EXIT_FAILURE);
    }

    IPC_Message msg;
    while (1) {
        if (read(fifo_fd, &msg, sizeof(IPC_Message)) > 0) {
            printf("[Bulb %d] Received command: %s\n", my_id, msg.command);

            // Mandatory latency simulation (1 to 3 seconds) to test concurrency
            sleep((rand() % 3) + 1);

            // Check if the command is a manual interaction (override)
            int is_override = (msg.sender_id == -1);

            // --- COMMAND INTERPRETATION (PARSER) ---

            if (strncmp(msg.command, "switch power on", 15) == 0) {
                if (!is_on) {
                    is_on = 1;
                    on_start_time = time(NULL); // Save the exact activation timestamp
                }
                send_response(msg.sender_id, "ACK: Bulb turned ON successfully", is_override);

            } else if (strncmp(msg.command, "switch power off", 16) == 0) {
                if (is_on) {
                    is_on = 0;
                    // Calculate how many seconds it was active and add to total time
                    total_on_time += (long)(time(NULL) - on_start_time);
                    on_start_time = 0;
                }
                send_response(msg.sender_id, "ACK: Bulb turned OFF successfully", is_override);

            } else if (strncmp(msg.command, "info", 4) == 0) {
                long current_total = total_on_time;
                if (is_on) {
                    // If currently active, dynamically add the elapsed partial time
                    current_total += (long)(time(NULL) - on_start_time);
                }
                char info_buffer[MAX_CMD_LEN];
                snprintf(info_buffer, sizeof(info_buffer), "State: %s | Registry: time=%lds parent=%d", 
                         is_on ? "ON" : "OFF", current_total, parent_id);
                send_response(msg.sender_id, info_buffer, is_override);
            
            } else if (strncmp(msg.command, "set_parent ", 11) == 0) {
                sscanf(msg.command, "set_parent %d", &parent_id);
                send_response(msg.sender_id, "ACK: Parent ID updated", is_override);
            
            } else {
                send_response(msg.sender_id, "ERR: Unsupported or invalid command", is_override);
            }
        }
    }

    return 0;
}
