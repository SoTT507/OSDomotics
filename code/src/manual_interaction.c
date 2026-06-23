#include "common.h"

int main(int argc, char *argv[]) {
    // base check of arguments
    if (argc < 3) {
        fprintf(stderr, "Usage: ./manual_interaction <target_id> <command> [arguments...]\n");
        fprintf(stderr, "Example: ./manual_interaction 1 switch power on\n");
        exit(EXIT_FAILURE);
    }

    int target_id = atoi(argv[1]);

    // rebuild of the command (joining the arguments with spaces)
    char full_command[MAX_CMD_LEN] = "";
    for(int i = 2; i < argc; i++) {
        // strncat to avoid buffer overflow
        strncat(full_command, argv[i], MAX_CMD_LEN - strlen(full_command) - 1);
        if(i < argc - 1) {
            strncat(full_command, " ", MAX_CMD_LEN - strlen(full_command) - 1);
        }
    }

    // FIFO path for device building
    char fifo_path[128];
    snprintf(fifo_path, sizeof(fifo_path), "%s%d.fifo", FIFO_PATH_PREFIX, target_id);

    // opening FIFO in write-only mode
    int fd = open(fifo_path, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "Error: Device %d does not exist or the FIFO is not ready (%s).\n", target_id, fifo_path);
        exit(ERR_DEVICE_NOT_FOUND); // Definito in common.h
    }

    // setup of binary IPC structure
    IPC_Message msg;
    msg.sender_id = -1; // -1 means an OVERRIDE from external source (not the Controller)
    msg.target_id = target_id;
    
    // Copy in order to avoid buffer overflow
    strncpy(msg.command, full_command, MAX_CMD_LEN - 1);
    msg.command[MAX_CMD_LEN - 1] = '\0'; // forced termination for safety

    // writing of the struct bytes on the pipe
    if (write(fd, &msg, sizeof(IPC_Message)) == -1) {
        perror("Error during command sending on FIFO");
        close(fd);
        exit(ERR_PIPE_BROKEN);
    }

    printf("[Manual Override] Command sent successfully to Device %d: '%s'\n", target_id, full_command);

    // final clean
    close(fd);
    return SUCCESS;
}