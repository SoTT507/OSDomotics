#!/bin/bash

if [ "$#" -lt 2 ]; then
    echo "Usage: ./manual_interaction.sh <id> <command> [parameters]"
    echo "Example: ./manual_interaction.sh 1 switch power on"
    exit 1
fi

TARGET_ID=$1
shift
COMMAND="$*" # Concatenate the rest of the arguments

FIFO_PATH="/tmp/domotics_dev_${TARGET_ID}.fifo"

if [ ! -p "$FIFO_PATH" ]; then
    echo "Error: Device ${TARGET_ID} does not exist or FIFO is not ready."
    exit 1
fi

# We construct the raw bytes of the C struct (IPC_Message)
# Now, as Bash writing directly to a C-struct binary pipe is tricky,
# a trick would be is to send purely formatted strings
# or use a small C helper program here.
# For now, we write the string directly when we will adapt the device to read strings,
# OR we can compile a tiny C script to format the struct.

# If we modify read() to accept raw strings for simplicity:
echo "$COMMAND" >"$FIFO_PATH"
echo "Sent '$COMMAND' to Device $TARGET_ID"
