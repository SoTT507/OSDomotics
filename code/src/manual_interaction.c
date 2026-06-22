#include "common.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./manual_interaction <target_id> <command> [args...]\n");
        exit(EXIT_FAILURE);
    }

    int target_id = atoi(argv[1]);

    // Rebuild the command using strncat (before was strcat) to avoid buffer overflows
    char full_command[MAX_CMD_LEN] = "";
    for(int i = 2; i < argc; i++) {
        strncat(full_command, argv[i], MAX_CMD_LEN - strlen(full_command) - 1);
        if(i < argc - 1) {
            strncat(full_command, " ", MAX_CMD_LEN - strlen(full_command) - 1);
        }
    }

    char fifo_path[128];
    sprintf(fifo_path, "%s%d.fifo", FIFO_PATH_PREFIX, target_id);

    // Open the device's pipe in write-only mode
    int fd = open(fifo_path, O_WRONLY);
    if (fd == -1) {
        perror("Error: Device does not exist or FIFO is not ready");
        exit(EXIT_FAILURE);
    }

    // Prepare the binary structure to send
    IPC_Message msg;
    msg.sender_id = -1; // -1 indicates an EXTERNAL MANUAL OVERRIDE (NOT the Controller) --> allows devices to 
                        //check msg.sender_id ot know instantly if it's manual interaction
    msg.target_id = target_id;
    strncpy(msg.command, full_command, MAX_CMD_LEN - 1);
    msg.command[MAX_CMD_LEN - 1] = '\0';

    // Write the exact bytes of the struct to the pipe
    if (write(fd, &msg, sizeof(IPC_Message)) == -1) {
        perror("Failed to send command");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("[Manual Override] Sent '%s' to Device %d via binary IPC\n", full_command, target_id);
    close(fd);
    return SUCCESS;
}