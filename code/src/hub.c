#include "common.h"
#include <time.h>

int my_id;
int parent_id = 0;
char my_fifo[128];
int fifo_fd;

int children[MAX_DEVICES];
int num_children = 0;

void cleanup_and_exit(int sig) {
    (void)sig;
    printf("\n[Hub %d] Shutting down...\n", my_id);
    close(fifo_fd);
    unlink(my_fifo);
    exit(SUCCESS);
}

int send_to_child(int child_id, const char* cmd_string) {
    char fifo_path[128];
    snprintf(fifo_path, sizeof(fifo_path), "%s%d.fifo", FIFO_PATH_PREFIX, child_id);
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd != -1) {
        IPC_Message msg = {0};
        msg.sender_id = my_id;
        msg.target_id = child_id;
        strncpy(msg.command, cmd_string, MAX_CMD_LEN - 1);
        msg.command[MAX_CMD_LEN - 1] = '\0';
        
        ssize_t bytes_written = 0;
        char *ptr = (char *)&msg;
        while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
            ssize_t w = write(fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
            if (w == -1) {
                if (errno == EINTR) continue;
                close(fd);
                return ERR_PIPE_BROKEN;
                break;
            }
            bytes_written += w;
        }
        close(fd);
        return SUCCESS;
    }
    return ERR_DEVICE_NOT_FOUND;
}

void send_response(int requester_id, const char* response_str, int is_override) {
    char target_fifo[128];
    char final_message[MAX_CMD_LEN];  

    if (is_override) {
        snprintf(final_message, MAX_CMD_LEN, "OVERRIDE (Manual): %s", response_str);
    } else {
        strncpy(final_message, response_str, MAX_CMD_LEN - 1);
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

int get_logical_state(const char* info_str) {
    if (strstr(info_str, "Status: ON") || strstr(info_str, "Status: OPEN")) return 1;
    if (strstr(info_str, "Status: OFF") || strstr(info_str, "Status: CLOSED")) return 0;
    return -1; 
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./hub <id>\n");
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

    printf("[Hub %d] Ready. Listening on %s\n", my_id, my_fifo);
    fifo_fd = open(my_fifo, O_RDWR | O_NONBLOCK);

    IPC_Message msg;
    while (1) {
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

            // ignores asynnchronous ACK and INFO to avoid loop
            if (strncmp(msg.command, "ACK:", 4) == 0 || strncmp(msg.command, "OVERRIDE", 8) == 0 || strncmp(msg.command, "INFO:", 5) == 0) {
                continue;
            }

            // if recieving error from child --> forward to Controller (ID = 0)
            if (strncmp(msg.command, "ERR:", 4) == 0) {
                send_response(0, msg.command, 0); // 0 indicates the Controller's FIFO
                continue;
            }
          
          
            printf("[Hub %d] Received command: %s\n", my_id, msg.command);
            sleep((rand() % 3) + 1); 

            int is_manual_override = (msg.sender_id == -1);

            if (strncmp(msg.command, "set_parent ", 11) == 0) {
                sscanf(msg.command, "set_parent %d", &parent_id);
            }
            else if (strncmp(msg.command, "add_child ", 10) == 0) {
                int child_id;
                if (sscanf(msg.command, "add_child %d", &child_id) == 1) {
                    int already_linked = 0;
                    for (int i = 0; i < num_children; i++) {
                        if (children[i] == child_id) {
                            already_linked = 1;
                            break;
                        }
                    }
                    if (!already_linked && num_children < MAX_DEVICES) {
                        children[num_children++] = child_id;
                        printf("[Hub %d] Linked child %d\n", my_id, child_id);
                    }
                }
            }
            else if (strncmp(msg.command, "switch ", 7) == 0) {
                int contact_failures = 0;
                for (int i = 0; i < num_children; i++) {
                    if (send_to_child(children[i], msg.command) != SUCCESS) {
                        contact_failures++;
                    }
                }
                if (contact_failures > 0) {
                    char err_msg[MAX_CMD_LEN];
                    snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Hub %d failed to reach %d child device(s).", ERR_PROCESS_CRASHED, my_id, contact_failures);
                    send_response(msg.sender_id, err_msg, is_manual_override);
                    continue;
                }
                char response[MAX_CMD_LEN];
                snprintf(response, sizeof(response), "ACK: Hub %d propagated action to %d children", my_id, num_children);
                send_response(msg.sender_id, response, is_manual_override);
            }
            else if (strncmp(msg.command, "info", 4) == 0) {
                if (num_children == 0) {
                    send_response(msg.sender_id, "INFO: Hub Status: EMPTY | Connected: 0", is_manual_override);
                    continue;
                }

                int contact_failures = 0;
                for (int i = 0; i < num_children; i++) {
                    if (send_to_child(children[i], "info") != SUCCESS) {
                        contact_failures++;
                    }
                }

                int states_match = 1;
                int first_logical_state = -1;
                int collected = 0;
                int manual_override_seen = 0;
                int expected_replies = num_children - contact_failures;
                time_t start_wait = time(NULL);

                while (collected < expected_replies && difftime(time(NULL), start_wait) < 5.0) {
                    IPC_Message child_reply;
                    if (read(fifo_fd, &child_reply, sizeof(IPC_Message)) == sizeof(IPC_Message)) {
                        int current_state = get_logical_state(child_reply.command);
                        if (current_state < 0) {
                            manual_override_seen = 1;
                        }
                        if (collected == 0) {
                            first_logical_state = current_state;
                        } else if (first_logical_state != current_state) {
                            states_match = 0;
                        }
                        collected++;
                    } else {
                        usleep(10000); 
                    }
                }

                char info_buffer[MAX_CMD_LEN];
                if (contact_failures > 0 || collected < expected_replies) {
                  snprintf(info_buffer, sizeof(info_buffer), "ERR (Code %d): Hub %d could not collect all child states.", ERR_PROCESS_CRASHED, my_id);
                } else if (!states_match || manual_override_seen || first_logical_state < 0) {
                  snprintf(info_buffer, sizeof(info_buffer), "INFO: Hub ID %d | Status: MANUAL OVERRIDE (Code %d) | Connected: %d", my_id, ERR_MANUAL_OVERRIDE, num_children);
                } else {
                  snprintf(info_buffer, sizeof(info_buffer), "INFO: Hub ID %d | Status: %s | Connected: %d", my_id, (first_logical_state == 1) ? "ON/OPEN" : "OFF/CLOSED", num_children);
                }
                send_response(msg.sender_id, info_buffer, is_manual_override);
            }
            else if (strcmp(msg.command, "del") == 0) {
                // Cascading della cancellazione ai figli tramite IPC
                for (int i = 0; i < num_children; i++) {
                    (void)send_to_child(children[i], "del");
                }
                usleep(50000); // Piccola pausa per assicurare l'invio IPC
                cleanup_and_exit(SIGTERM);
            }
            else {
                char err_msg[MAX_CMD_LEN];
                snprintf(err_msg, sizeof(err_msg), "ERR (Code %d): Unsupported command for Hub %d.", ERR_INVALID_COMMAND, my_id);
                send_response(msg.sender_id, err_msg, is_manual_override);
            }
        } 
    }
    return 0;
}