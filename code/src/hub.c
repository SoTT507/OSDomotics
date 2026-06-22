#include "common.h"
#include <time.h>

#define MAX_CHILDREN 10

int my_id;
int is_on = 0; // 0 = off, 1 = on
char my_fifo[128];
int fifo_fd;
int parent_fd;
int parent_id;

Device routing_table[MAX_CHILDREN];
int children = 0;

typedef enum {OFF, ON, INCON = -1} state;

void cleanup_and_exit(int sig) {
  printf("\n[Timer %d] Shutting down...\n", my_id);
  close(fifo_fd);
  unlink(my_fifo); // Remove named pipe from filesystem
  exit(0);
}

int send_ctl_ack(char *ack_str) {
  IPC_Message ack_msg = {my_id, 0, ack_str};
  if (write(parent_fd, (char*)&ack_msg, sizeof(IPC_Message)) == -1) {
    perror("send_ctl_ack()");
    return ERR_PIPE_BROKEN;
  }
  return SUCCESS;
}

// or: link(Device const *child)
int link(int const child_id) {
  if (children == MAX_CHILDREN) {
    return ERR_LINK_FAILED;
  }

  char *child_path;
  sprintf(child_path, "%s%d", FIFO_PATH_PREFIX, child_id);
  child_fd = open(child_path, O_WRONLY);

  routing_table[children].logical_id = child_id;
  routing_table[i].is_active = 1;

  return SUCCESS;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./hub <id>\n");
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

  printf("[Hub %d] Ready. Listening on %s\n", my_id, my_fifo);

  // open FIFO for reading (blocks until a writer connects)
  // using O_RDWR prevents EOF when the writer closes the pipe
  fifo_fd = open(my_fifo, O_RDWR);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  init_routing_table(routing_table, MAX_CHILDREN);
  IPC_Message msg;
/*
  fd_set read_fds;

  int max_fd = fifo_fd;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_SET(fifo_fd, &read_fds);
*/
  while (1) {
    ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
    if (bytes > 0) {
      printf("[Hub %d] Received command: %s\n", my_id, msg.command);
      char **tokens = tokenise(msg.command);

      // simulate processing latency (1 to 3 seconds)
      sleep((rand() % 3) + 1);

      switch msg.sender_id {
        case parent_id:
          if (strcmp(tokens[0], "switch") == 0) {
            send_ipc_message(atoi(tokens[1]), my_id, msg.command);
            is_on = (strcmp(toks[3], "on") == 0) ? 1 : 0;
          } else if (strcmp(tokens[0], "link") == 0) {
            link(atoi(tokens[1]));
            send_ctl_ack("ack link\0");
          }

          send_ctl_ack("ack\0");
          break;

        case -1:
          if (strcmp(toks[0], "switch") == 0) {
            if (children == 0) {
              printf("Hub: No children to control\n");
              break;
            }

            if (
              strcmp("on", toks[3]) == 0 || 
              strcmp("off", toks[0]) == 0
            ) 
            {
              for (int i=0; i < children; ++i) {
                send_ipc_message(
                  routing_table[i].logical_id, 
                  my_id,
                  msg.command
                );
              }
            }
            
          }
          break;

        default:
          int child_id = find_device_index(msg.sender_id);
          if (child_id == -1) break;
          if (msg.target_id != my_id) {
            send_ipc_message(msg.target_id, msg.sender_id, msg.command);
          } else {
            // handle valid child-parent cmds/notifications
          }
      }
    }
  }

      // TODO: Send acknowledgment back to Controller via CONTROLLER_FIFO

  return 0;
}
