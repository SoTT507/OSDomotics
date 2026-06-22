#!/bin/bash

# Extract the primary command (build, clean, run)
COMMAND=$1
shift
# Capture any remaining arguments into the ARGS variable
ARGS="$@"

case "$COMMAND" in
build)
    echo "Compilation..."
    # Compiliamo tutti i componenti nella cartella corrente
    gcc -Wall -Wextra -std=gnu17 -g src/controller.c -o controller
    gcc -Wall -Wextra -std=gnu17 -g src/bulb.c -o bulb
    gcc -Wall -Wextra -std=gnu17 -g src/window.c -o window
    gcc -Wall -Wextra -std=gnu17 -g src/fridge.c -o fridge
    echo "Compiled successfully!"
    ;;

clean)
    echo "Cleanup..."
    # Rimuoviamo tutti gli eseguibili inclusi i nuovi dispositivi
    rm -f controller bulb window fridge manual_interaction
    # Rimuoviamo le pipe (FIFO) dell'IPC
    rm -f /tmp/domotics_*.fifo
    rm -f /tmp/test_stdin.fifo
    echo "System reset complete"
    ;;
    
run)
    echo "================================================"
    echo "             DOMOTICS OS - DEMO"
    echo "================================================"

    # Compile everything cleanly
    $0 clean
    $0 build

    # Setup persistent FIFO
    rm -f /tmp/test_stdin.fifo
    mkfifo /tmp/test_stdin.fifo
    exec 3<> /tmp/test_stdin.fifo

    # Start the controller in the background
    ./controller < /tmp/test_stdin.fifo &
    CONTROLLER_PID=$!
    sleep 1

    send_cmd() {
        echo -e "\n>>> CMD: $1"
        echo "$1" >&3
        sleep $2 
    }

    # Evaluate the ARGS passed by the user (defaults to 'basic' if empty)
    SCENARIO="${ARGS:-basic}"

    echo "Executing Scenario: [$SCENARIO]"

    case "$SCENARIO" in
        basic)
            echo -e "\n--- RUNNING BASIC TOPOLOGY ---"
            send_cmd "add hub" 2
            send_cmd "add bulb" 2
            send_cmd "link 2 to 1" 2
            send_cmd "switch 1 power on" 3
            send_cmd "list" 2
            send_cmd "exit" 2
            ;;
            
        stress)
            echo -e "\n--- RUNNING DEEP STRESS TEST ---"
            send_cmd "add hub" 2     
            send_cmd "add timer" 2   
            send_cmd "add bulb" 2    
            send_cmd "add bulb" 2    
            send_cmd "link 3 to 1" 2  
            send_cmd "link 4 to 1" 2  
            send_cmd "switch 1 power on" 4
            send_cmd "info 1" 2
            send_cmd "del 1" 3
            send_cmd "list" 2
            send_cmd "exit" 2
            ;;
            
        error_check)
            echo -e "\n--- RUNNING ERROR PREVENTION TEST ---"
            send_cmd "add hub" 2
            send_cmd "add timer" 2
            # Attempt to create a cycle
            send_cmd "link 1 to 2" 2
            send_cmd "link 2 to 1" 2 
            send_cmd "exit" 2
            ;;

        *)
            echo "Error: Unknown scenario '$SCENARIO'."
            echo "Available scenarios: basic, stress, error_check"
            send_cmd "exit" 1
            ;;
    esac

    # Cleanup
    exec 3>&- 
    rm -f /tmp/test_stdin.fifo
    wait $CONTROLLER_PID

    echo "================================================"
    echo "      SCENARIO [$SCENARIO] COMPLETED"
    echo "================================================"
    ;;

*)
    # If user insert invalid command, show how the script has to be used
    echo "Usage: ./build.sh {build|clean|run} [ARGS...]"
    exit 1
    ;;
esac

exit 0
