#include "common.h"

int main() {
  char input[MAX_CMD_LEN];

  printf("--- Domotics System Powered On ---\n");
  printf("Digit 'help' to view all commands or 'exit' to quit.\n");

  // infinite loop for interactive shell
  while (1) {
    printf("domotics> ");

    // Read user input. If invalid --> exit
    if (fgets(input, MAX_CMD_LEN, stdin) == NULL) {
      break;
    }

    // removing \n character form string's end
    input[strcspn(input, "\n")] = 0;

    // if user press enter to nothing --> ignore
    if (strlen(input) == 0) {
      continue;
    }

    // --- Input Management ---
    if (strcmp(input, "exit") == 0) {
      printf("Powering off...\n");
      break;
    } else if (strncmp(input, "add", 3) == 0) {
      // strncmp checks only first 3 characters to see if there is "add"
      printf("You requested to add a new device. (TODO)\n");
      // future fork() in order to create a new child process
    } else if (strcmp(input, "list") == 0) {
      printf("Device list. (TODO)\n");
    } else {
      printf("Error: Command not found (Code: %d).\n", ERROR_INVALID_COMMAND);
    }
  }

  // TODO: send close signals to all of the child processes before terminating

  return SUCCESS;
}
