# Operating Systems Project 2026-2 - Domotics

This repository contains the implementation of "Project 2026-2 - Domotics" for the Operating Systems course. The project consists of a home automation system emulator written in **C and Bash**.

Each device in the system is represented by a distinct OS-level process. The process architecture is **flat**: all devices are direct children of a single main process called `Controller`. However, the system supports a **logical hierarchy** (control devices managing other devices) maintained and routed exclusively via Inter-Process Communication (IPC).

---

## Prerequisites

The project is designed to be compiled and executed in a **Linux** environment (tested on Ubuntu 24.04).
The following are required:
* `gcc` compiler (tested on GCC 15.2)
* `make` (if using the Makefile)
* `bash`

---

## Compilation and Execution

The project includes an automated system for compiling, running and cleaning the environment, manageable via `Makefile` (or via the `./build.sh` script).

* Building:
  * Using Makefile: `make build`
  * Using Bash: `./build.sh build`
 
* Cleaning:
  * Using MakeFile: `make clean`
  * Using Bash: `./build.sh clean`

* Running:
  * Using Makefile: `make run`
  * Using Bash: `./build.sh run`
  * **NOTE:** It's possible to pass additional arguments to the script using the ARGS or SCENARIO variable in order to run various "demo" scenario(e.g., `make run SCENARIO=sandbox`) between:
    * `basic`
    * `stress`
    * `sandbox`

---

## Common Commands
The main process (Controller) provides an interactive shell to centrally manage and command the entire system. The available commands are:
* `list`: lists all devices currently available in the system, diplaying their unique ID and their characteristics
* `add <device>`: spawns a new device process of the specified type (e.g., `add bulb`) as an OS-child of the controller and prints its details
* `del <id>`: logically and physically terminates the device process specified by the ID. If the target is a control device, it will send termination signals via IPC to all its children to cascade delete everything
* `link <id1> to <id2>`: updates the IPC routing so that device id1 is logically controlled by device `id2`. **NOTE:** `id2` must be a control device (Controller, Hub, Timer)
* `switch <id> <label> <pos>`: sets the switch `<label>` of device `<id>` to position `<pos>`
* `info <id>`: Displays the complete details, state, and registry of the specified device

## Device-specific Commands
Devices are divided into control Devices and Interaction Devices. Each has a state, specific switches and a registry for attributes
### Control Devices
* **Controller**: manages the entire system and acts as the OS-parent to all processes
  * **The current Controller logic assumes that any command is meant for a child device, which are spawned via fork() and then listed in the `routing_table`, therefore, if we type `switch 0 main off` or `info 0`, `Error: Device ID 0 not found` is returned (as controller is not listed in the routing table). (TO FIX according to specifications)
  * **Hub**: Allows connecting multiple devices
    * it mirrors the state and switches of its children. its state is only considered consistent after a command has been propagated to all children
    * Timer: Allows defining a schedule to control a single connected device or branch
      * Mirrors the state of its connected child
      * Registry: `begin` (activation time, format HH:MM) and `end` (deactivation time, format HH:MM)
### Interaction Devices
* **Bulb**
  * Switch: `power <on / off>`
  * Registry: `time` (total usage while on)
* **Window**
  * Switch: `open` and `close` (`on` / `off`) **NOTE:* these instantly return off after being triggered
  * Registry: `time` (total time left open)
* **Fridge**
  * Switch: `open` and `close` (`on` / `off`)
  * Registry: `time` (time left open), `delay` (time after which it automatically closes), `perc` (fill percentage 0-100%), `temp` (current internal temperature), `thermostat` (target temperature)
  * **NOTE:** the fill percentage (`perc`) and target temperature (`thermostat`) can ONLY be modified manually, bypassing the controller

---

## Manual Interaction
The system supports external manual interaction to simulate a physical human action on a device (e.g., physically pressing a button), which bypasses the root Controller. 
To issue a manual override, open a secondary terminal and run the dedicated executable:
`./manual_interaction <id> <command> [parameters]`
this executable communicates directly with the target device process (e.g., by writing directly to its named pipe).
