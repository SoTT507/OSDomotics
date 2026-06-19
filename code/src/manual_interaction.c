#include "common.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: ./manual_interaction <target_id> <command> [args...]\n");
        exit(EXIT_FAILURE);
    }

    int target_id = atoi(argv[1]);

    // rebuild the command by concatenating remaining arguments
    char full_command[MAX_CMD_LEN] = "";
    for(int i = 2; i < argc; i++) {
        strcat(full_command, argv[i]);
        if(i < argc - 1) strcat(full_command, " ");
    }

    char fifo_path[128];
    sprintf(fifo_path, "%s%d.fifo", FIFO_PATH_PREFIX, target_id);

    // open the device's pipe in write-only mode
    int fd = open(fifo_path, O_WRONLY);
    if (fd == -1) {
        perror("Error: Device does not exist or FIFO is not ready");
        exit(EXIT_FAILURE);
    }

    // prepare the binary structure to send
    IPC_Message msg;
    msg.sender_id = 0; // 0 o -1 per indicare un utente esterno/manual override
    msg.target_id = target_id;
    strncpy(msg.command, full_command, MAX_CMD_LEN - 1);
    msg.command[MAX_CMD_LEN - 1] = '\0';

    // write the exact bytes of the struct to the pipe
    if (write(fd, &msg, sizeof(IPC_Message)) == -1) {
        perror("Failed to send command");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("Sent '%s' to Device %d via binary IPC\n", full_command, target_id);
    close(fd);
    return SUCCESS;
}
