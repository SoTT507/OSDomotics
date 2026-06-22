#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

// ------------- Error codes ----------------

#define SUCCESS 0

// IPC and Routing Errors
#define ERR_DEVICE_NOT_FOUND 101
#define ERR_LINK_FAILED 102
#define ERR_DEVICE_TYPE_MISMATCH 103 // e.g., linking to an Interaction Device
#define ERR_CIRCULAR_LINK 104        // e.g., A -> B -> A

// Command and State Errors
#define ERR_INVALID_COMMAND 201
#define ERR_MANUAL_OVERRIDE 202 // Used when Hub detects inconsistent child states
#define ERR_UNSUPPORTED_SWITCH 203 // e.g., trying to change fridge 'perc' via Controller

// System Errors
#define ERR_PROCESS_CRASHED 301
#define ERR_PIPE_BROKEN 302
#define ERR_FORK_FAILED 303

//-----------------------------------------

#define MAX_CMD_LEN 256
#define FIFO_PATH_PREFIX "/tmp/domotics_dev_"
#define CONTROLLER_FIFO "/tmp/domotics_controller.fifo"

// --------------- STRUTTURE DATI ----------------

// IPC Message Structure
typedef struct {
  int sender_id;
  int target_id;
  char command[MAX_CMD_LEN];
} IPC_Message;

#define MAX_DEVICES 100

// Routing Table to keep track of devices
typedef struct {
  int logical_id;
  pid_t pid;
  char type[32];
  int is_active;
  int parent_id; // ID of the parent device (0 for Controller)
} Device;

//-----------------------------------------------

#endif
