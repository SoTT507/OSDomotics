#include "common.h"

int my_id;
int is_on = 0; // 0 = off, 1 = on
char my_fifo[128];
char *parent_fifo = CONTROLLER_FIFO;
int fifo_fd;
int parent_fd;

void cleanup_and_exit(int sig) {
  printf("\n[Bulb %d] Shutting down...\n", my_id);
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

int send_ctl_ack(char *ack_str){
  IPC_Message ack_msg = {my_id, 0, ack_str};
  if (write(parent_fd, (char*)&ack_msg, sizeof(IPC_Message)) == -1) {
    perror("send_ctl_ack()");
    return ERR_PIPE_BROKEN;
  }
  return SUCCESS;
}

int link(int const par_id){
  sprintf(parent_fifo, "%s%d", FIFO_PATH_PREFIX, par_id);
  int tmp_pfd = open(parent_fifo, O_WRONLY);

  if (tmp_pfd == -1) {
    // notify failed link
    return ERR_PIPE_BROKEN;
  }
  // or dup2(tmp_pfd, parent_fd)
  close(parent_fd);
  parent_fd = tmp_pfd;

  return SUCCESS;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./bulb <id>\n");
    exit(EXIT_FAILURE);
  }

  my_id = atoi(argv[1]);
  sprintf(my_fifo, "%s%d.fifo", FIFO_PATH_PREFIX, my_id);
  
  parent_fd = open(parent_fifo, O_WRONLY);
  if (parent_fd < 0) {
    perror("Failed to open parent's fifo");
    _exit(ERR_PIPE_BROKEN);
  }

  // termination
  signal(SIGTERM, cleanup_and_exit);
  signal(SIGINT, cleanup_and_exit);

  // FIFO for this specific device
  if (mkfifo(my_fifo, 0666) == -1 && errno != EEXIST) {
    perror("mkfifo failed");
    _exit(EXIT_FAILURE);
  }

  printf("[Bulb %d] Ready. Listening on %s\n", my_id, my_fifo);

  // open FIFO for reading (blocks until a writer connects)
  // using O_RDWR prevents EOF when the writer closes the pipe
  fifo_fd = open(my_fifo, O_RDWR);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  while (1) {
    ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
    if (bytes > 0) {
      printf("[Bulb %d] Received command: %s\n", my_id, msg.command);
      char **tokens = tokenise(msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      if (strncmp(msg.command, "switch power on", 15) == 0) {
        is_on = 1;
        printf("[Bulb %d] Status changed to ON\n", my_id);
      } else if (strncmp(msg.command, "switch power off", 16) == 0) {
        is_on = 0;
        printf("[Bulb %d] Status changed to OFF\n", my_id);
      } else if (strcmp(tokens[0], "link") == 0) {
        link(strtol(tokens[1], NULL, 10));
        send_ctl_ack("ack link\0");
      }
    }

    send_ctl_ack("ack\0");
      // TODO: Send acknowledgment back to Controller via CONTROLLER_FIFO
  }

  return 0;
}
