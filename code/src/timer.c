#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;
char my_fifo[128];
int fifo_fd;

int child_id = -1; // The timer controls only ONE child

// Timer schedule registry
int begin_h = -1, begin_m = -1;
int end_h = -1, end_m = -1;
int has_triggered_begin = 0;
int has_triggered_end = 0;

void cleanup_and_exit(int sig) {
    (void)sig;
    printf("\n[Timer %d] Shutting down...\n", my_id);
    close(fifo_fd);
    unlink(my_fifo);
    exit(SUCCESS);
}

void send_to_child(int target, const char* cmd_string) {
    char fifo_path[128];
    snprintf(fifo_path, sizeof(fifo_path), "%s%d.fifo", FIFO_PATH_PREFIX, target);
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd != -1) {
        IPC_Message msg;
        msg.sender_id = my_id;
        msg.target_id = target;
        strncpy(msg.command, cmd_string, MAX_CMD_LEN);
        
        ssize_t bytes_written = 0;
        char *ptr = (char *)&msg;
        while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
            ssize_t w = write(fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
            if (w == -1) {
                if (errno == EINTR) continue;
                break;
            }
            bytes_written += w;
        }
        close(fd);
    }
}

void send_response(int requester_id, const char* response_str, int is_override) {
    char target_fifo[128];
    char final_message[MAX_CMD_LEN];  

    if (is_override) {
        snprintf(final_message, MAX_CMD_LEN, "OVERRIDE (Manual): %s", response_str);
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
        IPC_Message response;
        response.sender_id = my_id;
        response.target_id = (requester_id == -1) ? 0 : requester_id;
        strncpy(response.command, final_message, MAX_CMD_LEN);
        
        ssize_t bytes_written = 0;
        char *ptr = (char *)&response;
        while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
            ssize_t w = write(target_fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
            if (w == -1) {
                if (errno == EINTR) continue;
                break;
            }
            bytes_written += w;
        }
        close(target_fd);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./timer <id>\n");
        exit(EXIT_FAILURE);
    }

    my_id = atoi(argv[1]);
    snprintf(my_fifo, sizeof(my_fifo), "%s%d.fifo", FIFO_PATH_PREFIX, my_id);

    signal(SIGTERM, cleanup_and_exit);
    signal(SIGINT, cleanup_and_exit);
    srand(time(NULL) ^ (getpid() << 16));

    if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo failed");
        exit(EXIT_FAILURE);
    }

    printf("[Timer %d] Ready. Listening on %s\n", my_id, my_fifo);
    fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK); // Non-blocking!

    IPC_Message msg;
    time_t last_tick = time(NULL);

    while (1) {
        // --- BACKGROUND TICK LOGIC ---
        time_t now = time(NULL);
        if (difftime(now, last_tick) >= 1.0) {
            last_tick = now;
            
            if (child_id != -1) {
                struct tm *current_time = localtime(&now);
                
                // Trigger ON
                if (begin_h != -1 && current_time->tm_hour == begin_h && current_time->tm_min == begin_m) {
                    if (!has_triggered_begin) {
                        send_to_child(child_id, "switch power on");
                        send_to_child(child_id, "switch open on"); // Agnostic trigger
                        has_triggered_begin = 1;
                        has_triggered_end = 0; // Reset opposite flag
                    }
                }
                
                // Trigger OFF
                if (end_h != -1 && current_time->tm_hour == end_h && current_time->tm_min == end_m) {
                    if (!has_triggered_end) {
                        send_to_child(child_id, "switch power off");
                        send_to_child(child_id, "switch close on"); // Agnostic trigger
                        has_triggered_end = 1;
                        has_triggered_begin = 0;
                    }
                }
            }
        }

        // --- COMMAND PARSING ---
        ssize_t total_read = 0;
        char *ptr = (char *)&msg;

        while (total_read < (ssize_t)sizeof(IPC_Message)) {
            ssize_t bytes = read(fifo_fd, ptr + total_read, sizeof(IPC_Message) - total_read);
            if (bytes > 0) {
                total_read += bytes;
            } else if (bytes == 0 || (bytes == -1 && errno == EAGAIN)) {
                if (total_read == 0) usleep(10000); 
                break; 
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }

        if (total_read == sizeof(IPC_Message)) {
            if (strncmp(msg.command, "ACK:", 4) == 0 || strncmp(msg.command, "OVERRIDE", 8) == 0) continue;

            printf("[Timer %d] Received command: %s\n", my_id, msg.command);
            sleep((rand() % 3) + 1); 

            int is_manual_override = (msg.sender_id == -1);
            int h, m;

            // Link logic
            if (strncmp(msg.command, "set_parent ", 11) == 0) {
                sscanf(msg.command, "set_parent %d", &parent_id);
            }
            else if (strncmp(msg.command, "add_child ", 10) == 0) {
                sscanf(msg.command, "add_child %d", &child_id);
                printf("[Timer %d] Linked child %d\n", my_id, child_id);
            }
            
            // Set Schedule (e.g., "switch begin 14:30")
            else if (sscanf(msg.command, "switch begin %d:%d", &h, &m) == 2 || sscanf(msg.command, "begin %d:%d", &h, &m) == 2) {
                begin_h = h; begin_m = m;
                has_triggered_begin = 0;
                send_response(msg.sender_id, "ACK: Timer start schedule updated", is_manual_override);
            }
            else if (sscanf(msg.command, "switch end %d:%d", &h, &m) == 2 || sscanf(msg.command, "end %d:%d", &h, &m) == 2) {
                end_h = h; end_m = m;
                has_triggered_end = 0;
                send_response(msg.sender_id, "ACK: Timer end schedule updated", is_manual_override);
            }
            
            // Passthrough for manual device switches
            else if (strncmp(msg.command, "switch ", 7) == 0) {
                if (child_id != -1) {
                    send_to_child(child_id, msg.command);
                    send_response(msg.sender_id, "ACK: Command passed to child", is_manual_override);
                } else {
                    send_response(msg.sender_id, "ERR: No child linked to timer", is_manual_override);
                }
            }

            // Info querying
            else if (strncmp(msg.command, "info", 4) == 0) {
                char info_buffer[MAX_CMD_LEN];
                char b_str[16] = "--:--", e_str[16] = "--:--";
                
                if (begin_h != -1) snprintf(b_str, 16, "%02d:%02d", begin_h, begin_m);
                if (end_h != -1) snprintf(e_str, 16, "%02d:%02d", end_h, end_m);

                snprintf(info_buffer, sizeof(info_buffer), "INFO: Timer ID %d | Linked: %d | Begin: %s | End: %s", 
                         my_id, child_id, b_str, e_str);
                         
                send_response(msg.sender_id, info_buffer, is_manual_override);
            }
            else {
                send_response(msg.sender_id, "ERR: Unsupported command", is_manual_override);
            }
        } 
    }
    return 0;
}