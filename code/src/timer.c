#include "common.h"
#include <time.h>

int my_id;
int is_on = 0; // 0 = off, 1 = on
char my_fifo[128];
char *parent_fifo = CONTROLLER_FIFO;
int fifo_fd;
int parent_fd;

Device *child = NULL;
//int child_fd = 0;

typedef struct {
  struct tm begin,
            end;
} timer_reg;

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
  if (child) {
    return ERR_LINK_FAILED;
  }

  /*
  char *child_path;
  sprintf(child_path, "%s%d", FIFO_PATH_PREFIX, child_id);
  */
  //child_fd = open(child_path, O_WRONLY);
  child = (Device*) malloc(sizeof(Device));

  child->logical_id = child_id;
  child->is_active = 1;

  return SUCCESS;
}

int init_itimerspec(struct itimerspec *tspec, struct const *tm _tm) {
  time_t tmp;
  time(&tmp);
  struct tm *_tm1 = localtime(&tmp);
  _tm1->tm_sec = 0;
  _tm1->tm_min = _tm.tm_min;
  _tm1->tm_hour = _tm.tm_hour;
  
  tspec->it_value.tv_sec = difftime(mktime(_tm1), time(NULL));
  tspec->it_value.tv_nsec = 0;
  tspec->it_interval = {0, 0};

  return SUCCESS;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: ./timer <id>\n");
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

  printf("[Timer %d] Ready. Listening on %s\n", my_id, my_fifo);

  // open FIFO for reading (blocks until a writer connects)
  // using O_RDWR prevents EOF when the writer closes the pipe
  fifo_fd = open(my_fifo, O_RDWR);
  if (fifo_fd < 0) {
    perror("open fifo failed");
    exit(EXIT_FAILURE);
  }

  IPC_Message msg;
  timer_reg my_reg;
  fd_set read_fds,
         write_fds;
  int timer_0 = 0, timer_1 = 0;

  int max_fd = fifo_fd;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_SET(fifo_fd, &read_fds);

  while (1) {
    int b_set = select(max_fd + 1, &read_fds, &write_fds, NULL, NULL);
    if (b_set == -1); //handle error

    if (FD_ISSET(timer_0, &read_fds)) {
      send_ipc_message()
    } // power on child
    if (FD_ISSET(timer_1, &read_fds)); // "" off ""
    
    if (FD_ISSET(fifo_fd, &read_fds)) {
      ssize_t bytes = read(fifo_fd, &msg, sizeof(IPC_Message));
      if (bytes > 0) {
        printf("[Timer %d] Received command: %s\n", my_id, msg.command);
        char **tokens = tokenise(msg.command);

        // simulate processing latency (1 to 3 seconds)
        sleep((rand() % 3) + 1);

        switch msg.sender_id {
          case 0:
            if (strcmp(tokens[0], "switch") == 0) {
              send_ipc_message(atoi(tokens[1]), my_id, msg.command);
              is_on = (strcmp(toks[3], "on") == 0) ? 1 : 0;
            } else if (strcmp(tokens[0], "link") == 0) {
              link(atoi(tokens[1]));
              send_ctl_ack("ack link\0");
              //FD_SET(child_fd, &write_fds);
              //if (child_fd > max_fd) max_fd = child_fd;
            }

            send_ctl_ack("ack\0");
            break;

          case -1:
            if (strcmp(toks[0], "set") == 0) {
              if (child == NULL) {
                printf("Timer: No child to control\n");
                break;
              }
              // ... [parse, checkbegin and end times]
              strptime(msg.command, "set %H:%M %H:%M", &my_reg.begin, &my_reg.end);
              
              timer_0 = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
              timer_1 = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
              FD_SET(timer_0, &read_fds);
              FD_SET(timer_1, &read_fds);
              if (timer_0 > max_fd && timer_0 > timer_1) {
                max_fd = timer_0;
              } else if (timer_1 > max_fd && timer_1 > timer_0) {
                max_fd = timer_1;
              }

              struct itimerspec tspec_0, tspec_1;
              init_itimerspec(&tspec_0, my_reg.begin);
              init_itimerspec(&tspec_1, my_reg.end);
              
              timerfd_settime(timer_0, 0, &tspec_0, NULL);
              timerfd_settime(timer_1, 0, &tspec_1, NULL);
            }
            break;

          case child->logical_id:
            break;
        }
      }

      // TODO: Send acknowledgment back to Controller via CONTROLLER_FIFO
    }
  }

  return 0;
}
