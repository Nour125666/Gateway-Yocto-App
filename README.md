# ⚙️ Embedded Linux Sensor Communication Application

> A modular embedded Linux communication stack built with **C, C++, POSIX, Yocto, systemd, UART, TCP and MQTT**, validated end-to-end using **QEMU inside WSL2**.

This project demonstrates how multiple Linux userspace applications can work together as independent services to acquire sensor data, expose runtime configuration and transmit data over the network.

The repository currently contains the **Yocto application recipe (`recipes-apps`)** and its associated source files, configuration and systemd services.

---

## 🚀 Project at a Glance

**Languages:** `C` `C++`  
**Build System:** `Yocto` `BitBake`  
**Operating System:** `Embedded Linux`  
**IPC:** `Unix Domain Sockets`  
**Hardware Interface:** `UART / TTY / termios`  
**Networking:** `TCP` `MQTT`  
**Service Manager:** `systemd`  
**Testing:** `QEMU qemux86-64` `WSL2`  
**Target Direction:** `STM32MP1 / OpenSTLinux`

### Current Status

| Feature | Status |
|---|---|
| Yocto cross-compilation | ✅ Validated |
| QEMU boot | ✅ Validated |
| systemd startup | ✅ Validated |
| Sensor service | ✅ Validated |
| Communication agent | ✅ Validated |
| Unix-domain socket IPC | ✅ Validated |
| Interactive CLI | ✅ Validated |
| UART runtime configuration | ✅ Validated |
| Mock sensor mode | ✅ Validated |
| TCP transmission | ✅ Validated |
| QEMU → WSL2 networking | ✅ Validated |
| MQTT integration | ✅ Integrated |
| STM32MP1 hardware deployment | 🔜 Next step |

---

# 🧩 Architecture

The application is composed of three independent programs:

| Component | Language | Responsibility |
|---|---|---|
| `sensord` | C | UART acquisition, sensor handling and mock-data fallback |
| `agent` | C++ | TCP/MQTT communication and network configuration |
| `cli` | C | Runtime configuration and diagnostics |

```text
                       User / Console / SSH
                               │
                               ▼
                              CLI
                     ┌─────────┴─────────┐
                     │                   │
                     ▼                   ▼
              Agent Control       Sensor Control
                     │                   │
                     ▼                   ▼
                   agent              sensord
                     ▲                   │
                     │                   │
                     └──── Sensor Data ──┘
                              │
                       Unix Domain Socket
                              │
                     ┌────────┴────────┐
                     │                 │
                     ▼                 ▼
                    TCP               MQTT
```

The two backend applications are designed to run continuously as **systemd services**.

The user normally interacts only with the CLI.

---

# 📂 Repository Structure

This repository currently contains the **application recipe** part of the Yocto project.

```text
recipes-apps/
└── nour-app/
    ├── nour-app_1.0.bb
    │
    └── files/
        ├── sensord.c
        ├── agent.cpp
        ├── cli.c
        │
        ├── nour-agent.conf
        ├── nour-agent-start
        │
        ├── nour-sensord.service
        └── nour-agent.service
```

The existing filenames originate from the development project, while the software architecture itself is generic and reusable.

---

# 🐧 Yocto / OpenEmbedded Integration

The project follows the standard **Yocto/OpenEmbedded recipe structure**.

The BitBake recipe handles:

- C compilation
- C++ compilation
- cross-compilation
- external dependencies
- installation of binaries
- configuration files
- systemd unit installation
- application packaging

The communication stack depends on Eclipse Paho:

```bitbake
DEPENDS = " \
    paho-mqtt-c \
    paho-mqtt-cpp \
"
```

The application can be built with:

```bash
bitbake nour-app
```

When integrated into an image recipe, the applications are installed directly into the target Linux root filesystem.

---

# 🌍 Hardware Portability

The application is intentionally implemented using **standard Linux and POSIX interfaces** instead of directly accessing MCU registers.

This makes the userspace architecture portable between different Yocto-supported Linux-capable platforms.

Examples include:

```text
STM32MP1
NXP i.MX
TI Sitara
Raspberry Pi
x86 embedded platforms
QEMU targets
```

The application logic itself normally does not need to be rewritten when changing boards.

Board-specific work remains in the BSP layer:

```text
Yocto MACHINE
BSP layers
Linux kernel configuration
Device Tree
UART pinmux
device naming
network configuration
boot configuration
```

For example, an STM32MP1 platform may expose a UART as:

```text
/dev/ttySTM1
```

while another Linux platform might expose:

```text
/dev/ttyS1
/dev/ttyUSB0
/dev/ttyAMA0
```

The application simply operates on the configured Linux device path.

---

# 🧪 Development & Test Environment

The project was developed inside a **WSL2 Ubuntu environment**.

```text
Windows
   │
   ▼
WSL2 Ubuntu
   │
   ▼
Yocto / BitBake
   │
   ▼
QEMU qemux86-64
   │
   ▼
Embedded Linux Application
```

QEMU provides a hardware-independent validation stage before moving the software to the physical embedded target.

This allowed the complete userspace architecture to be tested without requiring the final STM32 board.

---

# ✅ QEMU Validation

The project was thoroughly tested using:

```text
WSL2
Yocto
Poky
QEMU qemux86-64
systemd
```

Validated functionality includes:

```text
Linux boot
systemd initialization
automatic service startup
Unix-domain socket IPC
interactive CLI
runtime UART configuration
runtime interval configuration
mock sensor fallback
TCP communication
QEMU-to-host networking
journalctl logging
```

A complete TCP test successfully transferred generated sensor values from the QEMU guest to the WSL2 host.

Example:

```text
79.40
79.50
79.60
79.70
79.80
```

The tested path was:

```text
sensord
   │
   ▼
Sensor / Mock Data
   │
   ▼
Unix Domain Socket
   │
   ▼
agent
   │
   ▼
TCP
   │
   ▼
QEMU Network
   │
   ▼
WSL2 Host
```

---

# 🔌 UART / Linux TTY

UART communication is handled through the standard Linux TTY subsystem.

The userspace application does **not** directly manipulate UART hardware registers.

```text
Physical UART
     │
     ▼
Linux UART Driver
     │
     ▼
TTY Subsystem
     │
     ▼
/dev/ttyXXX
     │
     ▼
POSIX / termios
     │
     ▼
sensord
```

The kernel and Device Tree handle:

```text
peripheral enablement
pin multiplexing
interrupts
hardware resources
driver binding
```

The application handles:

```text
baud rate
data format
sensor acquisition
runtime configuration
communication logic
```

The default format is:

```text
115200 baud
8 data bits
No parity
1 stop bit

8N1
```

---

# 🧪 Mock Sensor Mode

When the configured UART is unavailable, `sensord` automatically falls back to generated data.

Example:

```text
UART unavailable: /dev/ttySTM1
using mock data

79.40
79.50
79.60
```

This allows the entire software stack to be tested without physical sensor hardware.

It was especially useful during QEMU development because the virtual x86 target obviously does not expose an STM32 UART such as:

```text
/dev/ttySTM1
```

---

# 🔄 Inter-Process Communication

The applications communicate locally using **Unix domain sockets**.

The IPC layer handles:

- sensor data transfer
- agent configuration
- UART configuration
- status requests
- CLI commands

Conceptually:

```text
sensord
   │
   │ sensor data
   ▼
Unix Socket
   │
   ▼
agent
```

And:

```text
              CLI
             /   \
            /     \
           ▼       ▼
       sensord    agent
       control    control
```

This keeps local control communication separate from external TCP/MQTT networking.

---

# 🔀 Pipes vs Unix Domain Sockets

Linux pipes are ideal for simple streams:

```text
Process A ──────► Process B
```

For example:

```bash
program1 | program2
```

This project requires communication between **independent long-running processes**, including request/response commands.

Unix domain sockets provide:

```text
bidirectional communication
client/server semantics
independent processes
multiple endpoints
persistent interfaces
request/response communication
```

They are therefore better suited to this architecture than anonymous pipes.

---

# 🧵 Processes, Threads & Concurrency

The software is intentionally split into independent Linux processes:

```text
sensord
agent
cli
```

This provides:

- modularity
- fault isolation
- easier debugging
- independent testing
- clear responsibilities
- service supervision

Threads are used when one process needs to handle multiple operations concurrently.

For example:

```text
sensor acquisition
       +
control interface
       +
communication
```

---

# 🔐 Mutexes & Synchronization

Multithreaded applications often share runtime state.

Examples include:

```text
UART baud rate
UART device
sampling interval
network configuration
connection state
```

If multiple threads access this data simultaneously, race conditions can occur.

A mutex provides controlled access:

```text
Thread A
   │
   ▼
LOCK
   │
Modify shared configuration
   │
   ▼
UNLOCK


Thread B
   │
   └──── waits while locked
```

Synchronization is therefore an important aspect of robust Linux userspace design.

---

# ⏱️ Linux Scheduling

Linux schedules the project's processes and threads automatically.

The scheduler determines when applications such as:

```text
sensord
agent
CLI
systemd
network services
```

receive CPU execution time.

The current application uses standard Linux userspace scheduling.

For stronger timing requirements, Linux provides policies such as:

```text
SCHED_OTHER
SCHED_FIFO
SCHED_RR
```

Potential extensions include:

```text
thread priorities
CPU affinity
PREEMPT_RT
real-time scheduling
```

The current application does **not** claim hard real-time behavior.

---

# 💻 CLI

The CLI provides runtime control without restarting the services.

Start it with:

```bash
cli
```

Example:

```text
Embedded CLI
Type 'help' for commands.

> status
> interval 500
> uart show
> uart baud 57600
> tcp 10.0.2.2 5000
> mqtt 10.0.2.2 1883 sensor/data
> exit
```

Supported operations include:

```text
status

tcp <ip> <port>

mqtt <ip> <port> <topic>

interval <milliseconds>

uart show

uart baud <baud>

uart device <device>

uart reconnect
```

The CLI communicates with the already-running services through Unix domain sockets.

---

# ⚡ systemd Services

The sensor and communication applications are managed by `systemd`.

Installed services:

```text
nour-sensord.service
nour-agent.service
```

Boot sequence:

```text
POWER ON
   │
   ▼
Linux Kernel
   │
   ▼
systemd
   │
   ├───────────────┐
   ▼               ▼
sensord           agent
   │               │
   └──────┬────────┘
          │
          ▼
      System Ready
```

No manual:

```bash
sensord &
```

or:

```bash
agent ...
```

is required after boot.

---

# 📋 Service Management & Logging

Service state can be checked with:

```bash
systemctl status nour-sensord
systemctl status nour-agent
```

or:

```bash
systemctl is-active nour-sensord
systemctl is-active nour-agent
```

Expected:

```text
active
active
```

Logs can be inspected with:

```bash
journalctl -u nour-sensord
```

and:

```bash
journalctl -u nour-agent
```

This provides proper Linux service supervision rather than manually launching background processes.

---

# 📦 BitBake

BitBake transforms the application source code into target packages.

The recipe handles:

```text
source files
compiler configuration
C compilation
C++ compilation
external libraries
installation paths
systemd units
configuration files
packaging
```

Because BitBake uses the toolchain associated with the selected:

```text
MACHINE
```

the same application recipe can be cross-compiled for different target architectures.

---

# 🧠 Kernel vs Userspace

One of the main concepts demonstrated by the project is the separation between the **Linux kernel** and **userspace applications**.

### Kernel responsibilities

```text
hardware drivers
process scheduling
memory management
interrupts
network interfaces
device files
hardware resources
```

### Application responsibilities

```text
sensor logic
protocol logic
runtime configuration
CLI
data processing
network transmission
```

For UART:

```text
Hardware
   │
   ▼
Kernel Driver
   │
   ▼
TTY Subsystem
   │
   ▼
Device File
   │
   ▼
Userspace Application
```

This separation is fundamental to embedded Linux architecture.

---

# 🌐 Networking

The communication agent currently supports:

### TCP

Direct socket-based communication between two systems.

```text
Device ───────── TCP ─────────► Server
```

### MQTT

Broker-based publish/subscribe communication.

```text
Device
   │
   ▼
MQTT Broker
   │
   ├────► Application
   ├────► Database
   └────► Cloud Service
```

MQTT is implemented using **Eclipse Paho**.

HTTP is not currently part of the runtime communication path.

However, an HTTP/REST interface could later provide:

```text
configuration APIs
diagnostics
remote management
local web interface
cloud integration
```

---

# 🎯 Target Hardware

The next major target is:

```text
STM32MP157F-DK2
Cortex-A7
OpenSTLinux
Yocto Project
```

The userspace architecture should remain largely unchanged.

The target-specific work mainly involves:

```text
STM32 BSP
Yocto MACHINE
Device Tree
UART pinmux
UART device verification
network configuration
hardware testing
```

---

# 🔮 Future Improvements

Planned or possible extensions include:

- 🔐 SSH / Dropbear
- 💾 persistent configuration
- 🔌 real STM32 UART validation
- 🌳 Device Tree configuration
- 📡 MQTT broker validation
- 🔒 TLS
- 🐕 watchdog integration
- 📊 structured logging
- ❤️ health monitoring
- 🌐 HTTP / REST API
- 🧵 additional synchronization hardening
- ⏱️ real-time scheduling evaluation
- ⚡ PREEMPT_RT
- 🔄 OTA update integration

---

# 🛠️ Skills Demonstrated

This project brings together multiple areas of embedded Linux engineering:

### Languages
`C` · `C++`

### Linux
`POSIX` · `Processes` · `Threads` · `Scheduling` · `Mutexes`

### Hardware Interfaces
`UART` · `TTY` · `termios`

### IPC
`Unix Domain Sockets` · `Pipes Concepts`

### Networking
`TCP` · `MQTT`

### System Integration
`systemd` · `journald`

### Build System
`Yocto` · `BitBake` · `Cross Compilation`

### Validation
`QEMU` · `WSL2` · `Mock Hardware` · `Host/Guest Networking`

### Embedded Architecture
`Kernel / Userspace Separation` · `Device Files` · `BSP Integration`

---

# 📌 Project Status

> **QEMU userspace architecture: validated ✅**

The complete application architecture has been tested under:

```text
WSL2
   +
Yocto / Poky
   +
QEMU qemux86-64
   +
systemd
```

The next major milestone is deployment and validation on real **STM32MP1 hardware**.

---

# 📄 License

The current development recipe uses:

```bitbake
LICENSE = "CLOSED"
```

An explicit software license should be selected before publishing the project as reusable open-source software.
