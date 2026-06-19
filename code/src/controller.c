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
      if (strcmp(input, "help") == 0) {
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
      else if (strcmp(input, "list") == 0) {
        printf("[Controller] Executing 'list'... (TODO: query children via IPC)\n");
      }
      else if (strncmp(input, "add ", 4) == 0) {
        char device_type[32];
        // sscanf extracts the string after "add "
        if (sscanf(input, "add %31s", device_type) == 1) {
          printf("[Controller] Spawning new device of type: %s\n", device_type);
          // TODO: spawn_device(device_type);
        } else {
          printf("Error: Invalid syntax. Use: add <device>\n");
        }
      }
      else if (strncmp(input, "del ", 4) == 0) {
        int target_id;
        if (sscanf(input, "del %d", &target_id) == 1) {
          printf("[Controller] Terminating device ID: %d\n", target_id);
          // TODO: send SIGTERM and cascade deletion via IPC
        } else {
          printf("Error: Invalid syntax. Use: del <id>\n");
        }
      }
      else if (strncmp(input, "link ", 5) == 0) {
        int id1, id2;
        // The pattern "link %d to %d" does all the work for us
        if (sscanf(input, "link %d to %d", &id1, &id2) == 2) {
          printf("[Controller] Linking device %d to parent device %d\n", id1, id2);
          // TODO: update routing table and notify via IPC
        } else {
          printf("Error: Invalid syntax. Use: link <id1> to <id2>\n");
        }
      }
      else if (strncmp(input, "switch ", 7) == 0) {
        int target_id;
        char label[32], pos[32];
        if (sscanf(input, "switch %d %31s %31s", &target_id, label, pos) == 3) {
          printf("[Controller] Switching device %d: label '%s' to '%s'\n", target_id, label, pos);
          // TODO: send IPC message to target device
        } else {
          printf("Error: Invalid syntax. Use: switch <id> <label> <pos>\n");
        }
      }
      else if (strncmp(input, "info ", 5) == 0) {
        int target_id;
        if (sscanf(input, "info %d", &target_id) == 1) {
          printf("[Controller] Requesting info for device ID: %d\n", target_id);
          // TODO: query device state via IPC
        } else {
          printf("Error: Invalid syntax. Use: info <id>\n");
        }
      }
      else if (strcmp(input, "exit") == 0) {
        printf("Powering off...\n");
        // TODO: send close signals to all of the child processes before terminating
        break;
      }
      else {
        printf("Error: Command not found (Code: %d). Type 'help' for available commands.\n", ERR_INVALID_COMMAND);
      }
    }

    return SUCCESS;
}
