#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

Device routing_table[MAX_DEVICES];
int next_logical_id = 1;

volatile sig_atomic_t child_terminated = 0;

void init_routing_table() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        routing_table[i].is_active = 0;
    }
}

int find_device_index(int logical_id) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].is_active && routing_table[i].logical_id == logical_id) {
            return i;
        }
    }
    return -1;
}

void handle_sigchld(int sig) {
    (void)sig; 
    child_terminated = 1;
}

int send_ipc_message(int target_logical_id, int sender_id, const char* cmd_string) {
    char fifo_path[128];
    snprintf(fifo_path, sizeof(fifo_path), "%s%d.fifo", FIFO_PATH_PREFIX, target_logical_id);

    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        printf("Error: Unable to contact device %d (FIFO not found).\n", target_logical_id);
        return ERR_DEVICE_NOT_FOUND;
    }

    IPC_Message msg;
    msg.sender_id = sender_id;
    msg.target_id = target_logical_id;
    strncpy(msg.command, cmd_string, MAX_CMD_LEN);

    ssize_t bytes_written = 0;
    char *ptr = (char *)&msg;
    
    while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
        ssize_t w = write(fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
        if (w == -1) {
            if (errno == EINTR) continue; 
            if (errno == EAGAIN) {
                perror("Error: Device FIFO is full (EAGAIN)");
                close(fd);
                return ERR_PIPE_BROKEN;
            }
            perror("Error writing to device FIFO");
            close(fd);
            return ERR_PIPE_BROKEN;
        }
        bytes_written += w;
    }

    close(fd);
    return SUCCESS;
}

int check_for_cycles(int child_id, int new_parent_id) {
    if (child_id == new_parent_id) return 1; 

    int current_node = new_parent_id;
    while (current_node != 0) { 
        int idx = find_device_index(current_node);
        if (idx == -1) break; 
        
        int parent_of_current = routing_table[idx].parent_id;
        if (parent_of_current == child_id) return 1; 
        
        current_node = parent_of_current;
    }
    return 0; 
}

int main() {
    char input[MAX_CMD_LEN];
    int controller_fifo_fd;
    fd_set read_fds;

    init_routing_table();

    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Error: sigaction");
        exit(EXIT_FAILURE);
    }

    mkfifo(CONTROLLER_FIFO, 0666);
    controller_fifo_fd = open(CONTROLLER_FIFO, O_RDWR);

    if (controller_fifo_fd == -1) {
        perror("Error opening FIFO controller");
        exit(EXIT_FAILURE);
    }

    printf("--- Domotics System Powered On ---\n");
    printf("Digit 'help' to view all commands or 'exit' to quit.\n");
    printf("domotics> ");
    fflush(stdout); 

    while (1) {
        // 1. Gestione sicura dei segnali PRIMA della select
        if (child_terminated) {
            child_terminated = 0; 
            int status;
            pid_t pid;

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                for (int i = 0; i < MAX_DEVICES; i++) {
                    if (routing_table[i].is_active && routing_table[i].pid == pid) {
                        routing_table[i].is_active = 0;
                        if (WIFSIGNALED(status)) {
                            printf("\n[Alarm] Device ID %d has CRASHED (Code %d, Signal %d)!\n",
                                    routing_table[i].logical_id, ERR_PROCESS_CRASHED, WTERMSIG(status));
                        } else if (WIFEXITED(status)) {
                            printf("\n[Controller] Device ID %d (Type: %s, PID: %d) cleanly shut down.\n",
                                   routing_table[i].logical_id, routing_table[i].type, pid);
                        }
                        break;
                    }
                }
            }
            printf("domotics> ");
            fflush(stdout);
        }

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);       
        FD_SET(controller_fifo_fd, &read_fds); 

        int max_fd = (controller_fifo_fd > STDIN_FILENO) ? controller_fifo_fd : STDIN_FILENO;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        // 2. Protezione totale da EINTR per evitare false letture
        if (activity < 0) {
            if (errno == EINTR) continue; 
            perror("Error: select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            
            // 3. Lettura byte-by-byte per non ingoiare multipli comandi accodati dal bash script
            int i = 0;
            char c;
            ssize_t n;
            while (i < MAX_CMD_LEN - 1) {
                n = read(STDIN_FILENO, &c, 1);
                if (n <= 0) break;
                if (c == '\n' || c == '\r') break;
                input[i++] = c;
            }
            input[i] = '\0';

            // Uscita sicura tramite EOF del bash script
            if (n <= 0 && i == 0) break; 

            if (strlen(input) == 0) {
                printf("domotics> ");
                fflush(stdout);
                continue;
            }

            if (strcmp(input, "help") == 0) {
                printf("\n--- DOMOTICS SYSTEM COMMANDS ---\n");
                printf("  help                         : Shows this help menu.\n");
                printf("  list                         : Lists all devices, their unique ID, and summarizes characteristics.\n");
                printf("  add <device>                 : Spawns a new device process (e.g., 'add bulb', 'add hub').\n");
                printf("  del <id>                     : Logically and physically terminates the device process and its children.\n");
                printf("  link <id1> to <id2>          : Updates IPC routing so <id1> is logically controlled by <id2>.\n");
                printf("  switch <id> <label> <pos>    : Sets the switch <label> of device <id> to position <pos>.\n");
                printf("  info <id>                    : Displays the complete details of the device.\n");
                printf("  exit                         : Safely shuts down the Controller and all child processes.\n");
                printf("--------------------------------\n");
            }
            else if (strcmp(input, "list") == 0) {
                printf("--- ACTIVE DEVICES ---\n");
                for (int i = 0; i < MAX_DEVICES; i++) {
                    if (routing_table[i].is_active) {
                        printf("ID: %d | Type: %s | PID: %d\n", routing_table[i].logical_id, routing_table[i].type, routing_table[i].pid);
                    }
                }
            }
            else if (strncmp(input, "add ", 4) == 0) {
                char device_type[32];
                if (sscanf(input, "add %31s", device_type) == 1) {
                    int new_id = next_logical_id++;
                    pid_t pid = fork();

                    if (pid < 0) {
                        perror("Error in fork");
                    } else if (pid == 0) {
                        // 4. Chiusura dei file descriptor nel figlio per non impedire l'EOF allo script
                        close(STDIN_FILENO);
                        close(controller_fifo_fd);

                        char id_str[16];
                        sprintf(id_str, "%d", new_id);
                        char exec_path[64];
                        snprintf(exec_path, sizeof(exec_path), "./%s", device_type);
                        execl(exec_path, device_type, id_str, NULL);
                        printf("Error: unable to launch '%s'\n", device_type);
                        exit(ERR_FORK_FAILED); 
                    } else {
                        for (int i = 0; i < MAX_DEVICES; i++) {
                            if (!routing_table[i].is_active) {
                                routing_table[i].logical_id = new_id;
                                routing_table[i].pid = pid;
                                strcpy(routing_table[i].type, device_type);
                                routing_table[i].is_active = 1;
                                routing_table[i].parent_id = 0; 
                                break;
                            }
                        }
                        printf("[Controller] Spawned '%s' with logic ID %d (PID: %d)\n", device_type, new_id, pid);
                    }
                }
            }
            else if (strncmp(input, "del ", 4) == 0) {
                int target_id;
                if (sscanf(input, "del %d", &target_id) == 1) {
                    int index = find_device_index(target_id);
                    if (index != -1) {
                        printf("[Controller] Sending cascade termination to device ID %d...\n", target_id);
                        send_ipc_message(target_id, 0, "del"); // sending IPC command instead of kill
                    } else {
                        printf("Error: Device ID %d not found.\n", target_id);
                    }
                }
            }
            else if (strncmp(input, "link ", 5) == 0) {
                int id1, id2;
                if (sscanf(input, "link %d to %d", &id1, &id2) == 2) {
                    int idx1 = find_device_index(id1);
                    int idx2 = find_device_index(id2);

                    // Controllo esistenza dispositivi
                    if (idx1 == -1 || (id2 != 0 && idx2 == -1)) {
                        printf("Error (Code %d): One or both devices not found.\n", ERR_DEVICE_NOT_FOUND);
                    } 
                    // Controllo tipo dispositivo padre (deve essere Hub, Timer o Controller=0)
                    else if (id2 != 0 && strcmp(routing_table[idx2].type, "hub") != 0 && strcmp(routing_table[idx2].type, "timer") != 0) {
                        printf("Error (Code %d): Device %d (%s) cannot be a parent (Control Device required).\n", 
                               ERR_DEVICE_TYPE_MISMATCH, id2, routing_table[idx2].type);
                    } 
                    // Controllo cicli
                    else if (check_for_cycles(id1, id2)) {
                        printf("Error (Code %d): Circular link detected. Operation aborted.\n", ERR_CIRCULAR_LINK);
                    } 
                    else {
                        printf("[Controller] Linking devices: %d set to be child of %d\n", id1, id2);
                        routing_table[idx1].parent_id = id2;

                        // sending IPC commands with verification of the outcome
                        int res1 = send_ipc_message(id1, 0, "set_parent"); // we can only pass the command, the device will parse it
                        int res2 = (id2 != 0) ? send_ipc_message(id2, 0, "add_child") : SUCCESS;

                        if (res1 != SUCCESS || res2 != SUCCESS) {
                            printf("Error (Code %d): Link failed during IPC communication.\n", ERR_LINK_FAILED);
                        } else {
                            printf("[Controller] Link command sent successfully.\n");
                        }
                    }
                } else {
                    printf("Error (Code %d): Invalid syntax. Use: link <id1> to <id2>\n", ERR_INVALID_COMMAND);
                }
            }
            else if (strncmp(input, "switch ", 7) == 0) {
                int target_id;
                char label[32], pos[32];
                if (sscanf(input, "switch %d %31s %31s", &target_id, label, pos) == 3){
                    if (find_device_index(target_id) != -1) {
                        char cmd_buffer[MAX_CMD_LEN];
                        snprintf(cmd_buffer, sizeof(cmd_buffer), "switch %s %s", label, pos);
                        printf("[Controller] Sending command to device %d...\n", target_id);
                        send_ipc_message(target_id, 0, cmd_buffer); 
                    }else{
                        printf("Error: Device ID %d not found.\n", target_id);
                    }
                }else{
                    printf("Error: Invalid syntax. Use: switch <id> <label> <pos>\n");
                }
            }
            else if (strncmp(input, "info ", 5) == 0) {
                int target_id;
                if (sscanf(input, "info %d", &target_id) == 1) {
                    if (find_device_index(target_id) != -1) {
                        printf("[Controller] requesting info for device %d...\n", target_id);
                        send_ipc_message(target_id, 0, "info");
                    }
                } else {
                    printf("Error: Invalid syntax. Use: info <id>\n");
                }
            }
            else if (strcmp(input, "exit") == 0) {
                printf("Powering off...\n");
                for (int i = 0; i < MAX_DEVICES; i++) {
                    if (routing_table[i].is_active) {
                        kill(routing_table[i].pid, SIGTERM);
                    }
                }
                break;
            }
            else {
                printf("Error: Command not found (Code: %d). Type 'help' for available commands.\n", ERR_INVALID_COMMAND);
            }

            printf("domotics> ");
            fflush(stdout);
        }

        if (FD_ISSET(controller_fifo_fd, &read_fds)) {
            IPC_Message msg;
            ssize_t bytes_read = read(controller_fifo_fd, &msg, sizeof(IPC_Message));

            if (bytes_read > 0) {
                printf("\n[IPC IN] Da: %d | A: %d | Msg: %s\n", msg.sender_id, msg.target_id, msg.command);
                printf("domotics> ");
                fflush(stdout);
            }
        }
    }
    close(controller_fifo_fd);
    unlink(CONTROLLER_FIFO);
    return SUCCESS;
}