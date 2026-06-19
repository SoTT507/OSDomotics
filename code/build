#!/bin/bash

# Check which argument was passed to the script ($1 is the first word after ./build.sh)
case "$1" in
build)
    echo "Compilation..."
    # Create directory 'bin' for executables if does not exists
    mkdir -p bin
    # Compile controller.c using gcc and create 'controller' executable in 'bin' dir.
    gcc -Wall src/controller.c -o bin/controller
    echo "Compiled successfully! Executable in bin/controller"
    ;;

clean)
    echo "Cleanup..."
    # Remove 'bin' folder along with executables
    rm -rf bin
    # TODO: add commands to remove pipe (FIFO) for the IPC
    echo "System reset complete"
    ;;

run)
    echo "Starting controller..."
    ./bin/controller
    ;;

*)
    # If user insert invalid command, show how the script has to be used
    echo "Utilizzo: ./build.sh {build|clean|run}"
    exit 1
    ;;
esac

exit 0
