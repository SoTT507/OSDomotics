#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Extract the primary command (build, clean, run)
COMMAND=$1
shift
# Capture any remaining arguments into the ARGS variable
ARGS="$@"

case "$COMMAND" in
build)
    echo "Compilation..."
    gcc -Wall -Wextra -std=gnu17 -g src/controller.c -o controller
    gcc -Wall -Wextra -std=gnu17 -g src/bulb.c -o bulb
    gcc -Wall -Wextra -std=gnu17 -g src/window.c -o window
    gcc -Wall -Wextra -std=gnu17 -g src/fridge.c -o fridge
    gcc -Wall -Wextra -std=gnu17 -g src/hub.c -o hub
    gcc -Wall -Wextra -std=gnu17 -g src/timer.c -o timer
    gcc -Wall -Wextra -std=gnu17 -g src/manual_interaction.c -o manual_interaction
    echo "Compiled successfully!"
    ;;

clean)
    echo "Cleanup..."
    rm -f controller bulb window fridge hub timer manual_interaction
    rm -f /tmp/domotics_*.fifo
    rm -f /tmp/test_stdin.fifo
    echo "System reset complete"
    ;;
    
run)
    # Compile everything cleanly before running
    $0 clean
    $0 build
    
    # Delegate the execution to the dedicated script
    chmod +x run_demo.sh
    ./run_demo.sh "$ARGS"
    ;;

*)
    echo "Usage: ./build.sh {build|clean|run} [ARGS...]"
    exit 1
    ;;
esac

exit 0