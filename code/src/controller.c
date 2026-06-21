#include "common.h"

Device routing_table[MAX_DEVICES];
int next_logical_id = 1;

// initializes the routing table
void init_routing_table()
{
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        routing_table[i].is_active = 0;
    }
}

// finds the index of a device via logical ID
int find_device_index(int logical_id)
{
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (routing_table[i].is_active &&
            routing_table[i].logical_id == logical_id)
        {
            return i;
        }
    }
    return -1;
}

// signal manager for child processes (Crash or termination)
void handle_sigchld(int sig)
{
    int status;
    pid_t pid;

    // WNOHANG allows the controller to not block if there are multiple terminated
    // children
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // finds the device in the table via PID
        for (int i = 0; i < MAX_DEVICES; i++)
        {
            if (routing_table[i].is_active && routing_table[i].pid == pid)
            {
                routing_table[i].is_active = 0; // marks as inactive
                if (WIFSIGNALED(status))
                {
                    // the process was terminated unexpectedly (e.g. kill -9)
                    printf("\n[Alarm] Device ID %d (Tipo: %s, PID: %d) has CRASHED "
                           "(Segnale %d)!\n",
                           routing_table[i].logical_id, routing_table[i].type, pid,
                           WTERMSIG(status));
                    // TODO: here would be added the logic to alert via IPC eventual
                    // parent/children
                }
                break;
            }
        }
        break;
    }
}

// function to send an IPC message to a device
int send_ipc_message(int target_logical_id, int sender_id, const char* cmd_string) {
    char fifo_path[128];
    // builds the path of the device's FIFO (e.g., /tmp/domotics_dev_3.fifo)
    snprintf(fifo_path, sizeof(fifo_path), "%s%d.fifo", FIFO_PATH_PREFIX, target_logical_id);

    // opens the FIFO for writing only (we use O_NONBLOCK to avoid blocking the controller
    // if the device is not reading yet)
    int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        printf("Error: Unable to contact device %d (FIFO not found).\n", target_logical_id);
        return ERR_DEVICE_NOT_FOUND;
    }

    IPC_Message msg;
    msg.sender_id = sender_id;
    msg.target_id = target_logical_id;
    strncpy(msg.command, cmd_string, MAX_CMD_LEN);

    if (write(fd, &msg, sizeof(IPC_Message)) == -1) {
        perror("Error writing to device FIFO");
        close(fd);
        return ERR_PIPE_BROKEN;
    }

    close(fd);
    return SUCCESS;
}

int main()
{
    char input[MAX_CMD_LEN];
    int controller_fifo_fd;
    fd_set read_fds;

    init_routing_table();

    // sets the manager for terminated child processes
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags =
        SA_RESTART | SA_NOCLDSTOP; // SA_RESTART restarts select() if interrupted
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("Error: sigaction");
        exit(EXIT_FAILURE);
    }

    // controller FIFO creation
    mkfifo(CONTROLLER_FIFO, 0666);
    // open in O_RDWR --> otherwise, in O_RDONLY, read() would return EOF (0) as
    // soon as the last writer closes the pipe --> O_RDWR keeps the pipe open
    controller_fifo_fd = open(CONTROLLER_FIFO, O_RDWR);
    if (controller_fifo_fd == -1)
    {
        perror("Error opening FIFO controller");
        exit(EXIT_FAILURE);
    }

    printf("--- Domotics System Powered On ---\n");
    printf("Digit 'help' to view all commands or 'exit' to quit.\n");
    printf("domotics> ");
    fflush(stdout); // force the print to video before select()

    // infinite loop for interactive shell
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);       // fd 0: input (keyboard)
        FD_SET(controller_fifo_fd, &read_fds); // fd of FIFO for IPC messages

        int max_fd = (controller_fifo_fd > STDIN_FILENO) ? controller_fifo_fd : STDIN_FILENO;

        // select() awaits blocking until data is available on the keyboard or FIFO
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR)
        {
            perror("Error: select");
            break;
        }
        // IS A COMMAND ARRIVED FROM THE USER (KEYBOARD)?
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            if (fgets(input, MAX_CMD_LEN, stdin) == NULL)
                break;

            input[strcspn(input, "\n")] = 0; // remove \n
            if (strlen(input) == 0)
            {
                printf("domotics> ");
                fflush(stdout);
                continue;
            }

            // --- Input Management ---
            if (strcmp(input, "help") == 0)
            {
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
            else if (strcmp(input, "list") == 0)
            {
                printf("--- ACTIVE DEVICES ---\n");
                for (int i = 0; i < MAX_DEVICES; i++)
                {
                    if (routing_table[i].is_active)
                    {
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
                        // --- FATHER PROCESS (New Device) ---
                        char id_str[16];
                        sprintf(id_str, "%d", new_id);
                        
                        // setup the executable path (e.g., "./bulb")
                        char exec_path[64];
                        snprintf(exec_path, sizeof(exec_path), "./%s", device_type);

                        execl(exec_path, device_type, id_str, NULL);
                        
                        // If we arrive here, execl has failed
                        printf("Error: unable to launch '%s'\n", device_type);
                        exit(ERR_FORK_FAILED); 
                    } else {
                        // --- FATHER PROCESS (controller) ---
                        // adding to routing table
                        for (int i = 0; i < MAX_DEVICES; i++) {
                            if (!routing_table[i].is_active) {
                                routing_table[i].logical_id = new_id;
                                routing_table[i].pid = pid;
                                strcpy(routing_table[i].type, device_type);
                                routing_table[i].is_active = 1;
                                break;
                            }
                        }
                        printf("[Controller] Spawneed '%s' with logic ID %d (PID: %d)\n", device_type, new_id, pid);
                    }
                }
            }
            else if (strncmp(input, "del ", 4) == 0) {
                int target_id;
                if (sscanf(input, "del %d", &target_id) == 1) {
                    int index = find_device_index(target_id);
                    if (index != -1) {
                        printf("[Controller] Terminating device ID %d (PID %d)...\n", target_id, routing_table[index].pid);
                        // sends SIGTERM for clean termination --> the SIGCHLD handler will remove it from the routing table
                        kill(routing_table[index].pid, SIGTERM); 
                    } else {
                        printf("Error: Device ID %d not found.\n", target_id);
                    }
                }
            }
            else if (strncmp(input, "link ", 5) == 0)
            {
                int id1, id2;
                // The pattern "link %d to %d" does all the work for us
                if (sscanf(input, "link %d to %d", &id1, &id2) == 2)
                {
                    int idx1 = find_device_index(id1);
                    int idx2 = find_device_index(id2);
                    if (idx1 != -1 && idx2 != -1) {
                        // REQ: id2 MUST be a Control Device (hub, timer or Controller)
                        // the controller which is the first created, has ID 0, thus id2 == 0 is always valid
                        if (strcmp(routing_table[idx2].type, "hub") != 0 && strcmp(routing_table[idx2].type, "timer") != 0 && id2 != 0) {
                            
                            printf("Error (Code %d): The device %d (%s) is NOT a Control Device.\n", 
                            ERR_DEVICE_TYPE_MISMATCH, id2, routing_table[idx2].type);
                        }else{
                            char cmd_buffer[MAX_CMD_LEN];

                            printf("[Controller] Linking devices: %d set to be child of %d\n", id1, id2);

                            // sending IPC to the child device (id1) informing it of the new parent (id2)
                            snprintf(cmd_buffer, sizeof(cmd_buffer), "set_parent %d", id2);
                            send_ipc_message(id1, 0, cmd_buffer);

                            // sending IPC to the parent device (id2) informing it of the new child (id1)
                            // If the parent is the controller (id2 == 0), we don't send the IPC message, we just update our logical table.
                            if (id2 != 0) {
                                snprintf(cmd_buffer, sizeof(cmd_buffer), "add_child %d", id1);
                                send_ipc_message(id2, 0, cmd_buffer);
                            }
                            printf("[Controller] Link command sent successfully.\n");
                        }
                    } else {
                        printf("Errore: Impossible to find one or both devices (ID1: %d, ID2: %d).\n", id1, id2);
                    }
                } else {
                    printf("Error: Invalid syntax. Use: link <id1> to <id2>\n");
                }
            }
            else if (strncmp(input, "switch ", 7) == 0)
            {
                int target_id;
                char label[32], pos[32];
                if (sscanf(input, "switch %d %31s %31s", &target_id, label, pos) == 3){
                    if (find_device_index(target_id) != -1) {
                        char cmd_buffer[MAX_CMD_LEN];
                        // formatting the command to send to the device
                        snprintf(cmd_buffer, sizeof(cmd_buffer), "switch %s %s", label, pos);
        
                        printf("[Controller] Sending command to device %d...\n", target_id);
                        send_ipc_message(target_id, 0, cmd_buffer); // sender_id 0 = Controller
                    }else{
                        printf("Error: Device ID %d not found.\n", target_id);
                    }
                }else{
                    printf("Error: Invalid syntax. Use: switch <id> <label> <pos>\n");
                }
            }
            else if (strncmp(input, "info ", 5) == 0)
            {
                int target_id;
                if (sscanf(input, "info %d", &target_id) == 1) {
                    if (find_device_index(target_id) != -1) {
                        printf("[Controller] requesting info for device %d...\n", target_id);
                        send_ipc_message(target_id, 0, "info");
                    }
                }
                else
                {
                    printf("Error: Invalid syntax. Use: info <id>\n");
                }
            }
            else if (strcmp(input, "exit") == 0)
            {
                printf("Powering off...\n");
                // sends SIGTERM to all active devices
                for (int i = 0; i < MAX_DEVICES; i++) {
                    if (routing_table[i].is_active) {
                        kill(routing_table[i].pid, SIGTERM);
                    }
                }
                break;
            }
            else
            {
                printf("Error: Command not found (Code: %d). Type 'help' for available commands.\n", ERR_INVALID_COMMAND);
            }

            printf("domotics> ");
            fflush(stdout);
        }
        //  INCOMING MESSAGE IPC FROM A CHILD (FIFO)?
        if (FD_ISSET(controller_fifo_fd, &read_fds))
        {
            IPC_Message msg;
            ssize_t bytes_read = read(controller_fifo_fd, &msg, sizeof(IPC_Message));

            if (bytes_read > 0)
            {
                // TODO: manage IPC message (e.g. status update, device errors)
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
