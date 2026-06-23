#!/bin/bash

echo "================================================"
echo "             DOMOTICS OS - DEMO"
echo "================================================"

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

# Evaluate the argument passed by the user (defaults to 'basic' if empty)
SCENARIO="${1:-basic}"

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

        sandbox)
        echo -e "\n--- RUNNING SANDBOX ENVIRONMENT ---"
        # Prepariamo un ambiente ricco di dispositivi
        send_cmd "add hub" 1
        send_cmd "add timer" 1
        send_cmd "add bulb" 1
        send_cmd "add window" 1
        send_cmd "add fridge" 1
        
        # Creiamo una gerarchia: Hub controlla Bulb e Window. Timer controlla Fridge.
        send_cmd "link 3 to 1" 1
        send_cmd "link 4 to 1" 1
        send_cmd "link 5 to 2" 1
        send_cmd "list" 1
        
        echo -e "\n========================================================="
        echo "                SANDBOX READY"
        echo "========================================================="
        
        # Passiamo il controllo al tuo terminale
        while read -r user_input; do
            echo "$user_input" >&3
            if [ "$user_input" = "exit" ]; then
                break
            fi
        done
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