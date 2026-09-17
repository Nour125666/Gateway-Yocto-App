Embedded Linux Sensor Communication Application

This repository contains the application recipe of an embedded Linux communication project built with the Yocto Project.

It demonstrates practical use of C, C++, POSIX APIs, Unix domain sockets, UART/TTY, TCP, MQTT, systemd integration and BitBake in a small multi-process embedded Linux application.

The repository currently contains only the recipes-apps part of the project.

Architecture

The application is composed of three programs:

Component

Language

Role

sensord

C

Reads sensor data from UART/TTY and provides mock data when hardware is unavailable

agent

C++

Receives sensor data and forwards it through TCP or MQTT

cli

C

Provides runtime configuration and status commands

Simplified architecture:

                     User / Console
                           |
                           v
                          CLI
                 +---------+---------+
                 |                   |
                 v                   v
             agent control      sensor control
                 |                   |
                 v                   v
               agent             sensord
                 ^                   |
                 |                   |
                 +--- sensor data ---+
                         |
                         +---- TCP
                         |
                         +---- MQTT

The background processes can be started automatically by systemd, while the CLI is used interactively for configuration and diagnostics.

Repository Structure
```text
recipes-apps/
└── application/
    ├── application_1.0.bb
    └── files/
        ├── sensord.c
        ├── agent.cpp
        ├── cli.c
        ├── agent.conf
        ├── agent-start
        ├── sensor.service
        └── agent.service
```text
The actual filenames in the repository may retain the original development names, but the architecture is intentionally generic and reusable.

Yocto Repository Structure and Portability

This project follows the standard Yocto/OpenEmbedded recipe structure, which allows the application to be integrated into a larger Yocto layer and reused across different embedded Linux targets.

The application itself is intentionally kept mostly independent from the target hardware. Because it relies on standard Linux/POSIX interfaces, the same recipe and source architecture can be cross-compiled for different Yocto-supported boards, including platforms such as STM32MP1, provided that the corresponding BSP, machine configuration, Device Tree, and hardware interfaces are correctly configured.

In practice, moving from QEMU to a physical board should mainly require target-specific integration such as:

selecting the appropriate Yocto MACHINE

adding the board BSP layers

enabling the required UART in the Device Tree

verifying the target UART device name

adjusting network and boot configuration where necessary

The application logic itself does not need to be rewritten for each board.

Development and Test Environment

The project was developed and thoroughly tested in a WSL2 Linux development environment using the Yocto Project toolchain.

The complete userspace architecture was validated using QEMU (qemux86-64), including:

Yocto/BitBake cross-compilation

Linux boot with systemd

automatic startup of the sensor and communication services

Unix-domain socket IPC

CLI runtime control

UART mock fallback

runtime baud-rate and interval changes

TCP communication

QEMU-to-host networking

service logging through journalctl

This provides a hardware-independent validation stage before deploying the same application architecture to a physical embedded Linux board such as an STM32MP1 target.

Embedded C

sensord and cli are implemented in C.

The project demonstrates:

POSIX file descriptors

Unix domain sockets

UART/TTY access

termios

command parsing

process communication

error handling

runtime device configuration

UART access is performed through the Linux TTY subsystem rather than direct MCU register access.

Example Linux device:

/dev/ttySTM1

This keeps the application in userspace and makes it portable across real hardware, pseudo-terminals and emulated environments.

C++ Communication Agent

The communication agent is implemented in C++.

It handles:

sensor data reception

TCP communication

MQTT communication

runtime protocol switching

external library integration

background communication logic

The MQTT implementation uses Eclipse Paho.

Example startup modes:

agent tcp <ip> <port>
agent mqtt <ip> <port> <topic>

POSIX and Linux APIs

The project exercises several important Linux userspace concepts:

open()
read()
write()
close()
socket()
bind()
connect()
accept()
pthread
termios
Unix domain sockets

These interfaces form the bridge between the application and Linux kernel services.

UART / TTY

UART communication is handled using standard Linux APIs.

The kernel exposes the UART as a device file:

/dev/ttySTMx

The application configures communication parameters such as:

baud rate
data bits
parity
stop bits

through termios.

Example CLI interaction:

> uart show
Device: /dev/ttySTM1
Baud: 115200
Format: 8N1

> uart baud 57600
OK UART baud set to 57600

When the UART device is unavailable, the sensor process falls back to mock sensor values so the software stack can still be tested.

Inter-Process Communication

The project uses Unix domain sockets for communication between local processes.

They are used for:

sensor data transfer

agent control

UART configuration

status queries

Unix sockets were chosen instead of simple pipes because the architecture requires independent processes and bidirectional command/response communication.

Pipes vs Unix Sockets

A pipe is well suited to a simple one-way data stream:

Process A ---> Process B

Unix domain sockets allow a more flexible architecture:

Process A <----> Process B
Process C <----> Process B

They support client/server semantics while remaining local to the Linux system.

Threads, Scheduling and Mutexes

The application demonstrates important Linux concurrency concepts.

For example, one execution path may handle sensor acquisition while another handles configuration commands.

This introduces topics such as:

Linux task scheduling

blocking I/O

periodic execution

threads

shared state

race conditions

synchronization

A mutex is used or becomes necessary whenever multiple execution contexts can modify shared data such as:

UART configuration
communication mode
sampling interval
connection state

The current design uses normal Linux userspace scheduling.

For applications requiring stronger timing guarantees, possible future extensions include:

SCHED_FIFO
SCHED_RR
thread priorities
CPU affinity
PREEMPT_RT

CLI

The command-line interface provides runtime control without restarting the services.

Example:

$ cli

Embedded CLI
Type 'help' for commands.

> status
> interval 500
> uart show
> uart baud 57600
> tcp 10.0.2.2 5000
> mqtt 10.0.2.2 1883 sensor/data
> exit

This demonstrates command parsing and local IPC between the CLI and the running background processes.

systemd Integration

The recipe includes systemd service files for the sensor and communication processes.

The intended boot flow is:

Linux boot
   |
   v
systemd
   |
   +---- sensor service
   |
   +---- communication agent

This allows both applications to:

start automatically at boot

run in the background

restart after failure

expose logs through journalctl

Typical commands:

systemctl status <sensor-service>
systemctl status <agent-service>
journalctl -u <sensor-service>
journalctl -u <agent-service>

Yocto / BitBake

The recipe demonstrates how a custom userspace application is integrated into Yocto.

It covers:

compiling C and C++

cross-compilation

installing binaries into the target root filesystem

external dependency handling

systemd service installation

configuration file installation

packaging

Main application dependencies include:

paho-mqtt-c
paho-mqtt-cpp

The recipe can be built with:

bitbake <recipe-name>

Kernel and Userspace Separation

A central concept demonstrated by this project is the separation between hardware support and application logic.

Physical UART
     |
     v
Linux UART driver
     |
     v
TTY subsystem
     |
     v
/dev/ttySTMx
     |
     v
POSIX / termios
     |
     v
sensord

The Linux kernel and Device Tree handle:

peripheral enablement

pin multiplexing

hardware resources

driver binding

The userspace application handles:

UART configuration

sensor acquisition

protocol logic

application control

This separation is fundamental to embedded Linux design.

Networking

The communication agent currently supports:

TCP

MQTT

TCP provides a direct network stream.

MQTT provides broker-based publish/subscribe communication and is suitable for IoT and telemetry applications.

HTTP is not currently part of the implemented data path, but the architecture could later be extended with an HTTP or REST interface for:

configuration

diagnostics

local web APIs

cloud integration

QEMU Validation

The application stack has been validated in a Yocto qemux86-64 environment.

Verified functionality includes:

systemd service startup
sensor process startup
communication agent startup
Unix socket communication
interactive CLI
runtime interval changes
runtime UART configuration
mock sensor fallback
TCP transmission
QEMU-to-host networking

A complete TCP test successfully transferred generated sensor values from QEMU to a listener running on the host.

Example:

79.40
79.50
79.60
79.70

Skills Demonstrated

This project showcases practical experience with:

Languages

C

C++

Embedded Linux

POSIX APIs

processes

threads

Linux scheduling concepts

mutexes and synchronization

TTY

termios

Unix domain sockets

Networking

TCP

MQTT

System Integration

systemd

service supervision

journald

Yocto

BitBake recipes

dependency management

package installation

cross-compilation

Linux Architecture

kernel/userspace separation

Linux device files

TTY subsystem

Testing

QEMU

mock hardware

host/guest networking

Current Status

The application architecture has been validated under QEMU.

The next major steps are:

real UART validation on the target board

Device Tree UART configuration

persistent runtime configuration

SSH integration

MQTT broker validation

further synchronization hardening

target deployment on STM32MP1

License

The development recipe currently uses:

LICENSE = "CLOSED"

An explicit open-source license should be selected before publishing the project as reusable open-source software.
