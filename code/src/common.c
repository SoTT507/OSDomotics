#include "common.h"
#include <string.h>
#include <stdlib.h>

char **tokenise(char *str){
  char *toks[] = (char**) malloc(sizeof(char*) * (MAX_TOKENS + 1));
  toks[0] = strtok(str, " ");
  int i = 1;
  
  do {
    toks[i] = strtok(NULL, " ");
    ++i;
  }
  while(toks[i-1] != NULL && i < MAX_TOKENS);

  if(i == MAX_TOKENS) toks[i] = NULL;
  return toks;
}

void init_routing_table(Device devices[], int const MAX_DEVS){
  for (int i = 0; i < MAX_DEVS; ++i) {
    devices[i].is_active = 0;
  }
}

int find_device_index(int logical_id, Device devices[]){
  for (int i = 0; i < MAX_DEVICES; ++i){
    if (devices[i].is_active &&
      devices[i].logical_id == logical_id)
    {
      return i;
    }
  }
  return -1;
}

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

    // if (write(fd, &msg, sizeof(IPC_Message)) == -1) {
    //     perror("Error writing to device FIFO");
    //     close(fd);
    //     return ERR_PIPE_BROKEN;
    // }

    // Write loop that guarantee full transfer
    ssize_t bytes_written = 0;
    char *ptr = (char *)&msg;
    
    while (bytes_written < (ssize_t)sizeof(IPC_Message)) {
        ssize_t w = write(fd, ptr + bytes_written, sizeof(IPC_Message) - bytes_written);
        if (w == -1) {
            if (errno == EINTR) continue; // Interrupted by signal, retry
            if (errno == EAGAIN) {
                // Pipe is full. In a perfect world, we would queue this.
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