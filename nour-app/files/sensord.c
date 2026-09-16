#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#define SOCKET_PATH         "/tmp/nour-sensor.sock"
#define CONTROL_SOCKET_PATH "/tmp/nour-sensord-ctrl.sock"

#define DEFAULT_UART_DEVICE "/dev/ttySTM1"
#define DEFAULT_UART_BAUD   115200

#define MOCK_START_VALUE    33.0
#define MOCK_STEP           0.1
#define SEND_INTERVAL_SEC   1
#define UART_RETRY_SEC      5
#define UART_DEVICE_MAX     128

/* Shared UART configuration/state.
 * The control thread changes the requested configuration.
 * The main sensor loop owns uart_fd and performs the actual reopen.
 */
typedef struct {
    pthread_mutex_t lock;
    char device[UART_DEVICE_MAX];
    int baud;
    int connected;
    int reconfigure_requested;
} UartState;

typedef struct {
    char line[128];
    size_t pos;
} UartParser;

static UartState g_uart = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .device = DEFAULT_UART_DEVICE,
    .baud = DEFAULT_UART_BAUD,
    .connected = 0,
    .reconfigure_requested = 0
};

static speed_t baud_to_termios(int baud)
{
    switch (baud) {
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
        default:     return 0;
    }
}

static int uart_open(const char *device, int baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }

    speed_t speed = baud_to_termios(baud);
    if (speed == 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    cfmakeraw(&tty);

    /* 8N1, no hardware flow control. */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    tty.c_cflag |= CREAD | CLOCAL;

    /* Non-blocking reads. */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (cfsetispeed(&tty, speed) != 0 ||
        cfsetospeed(&tty, speed) != 0) {
        close(fd);
        return -1;
    }

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }

    tcflush(fd, TCIFLUSH);
    return fd;
}

static int uart_read_value(int fd, UartParser *parser, double *value)
{
    for (;;) {
        char c;
        ssize_t n = read(fd, &c, 1);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }

        if (n == 0)
            return 0;

        if (c == '\r')
            continue;

        if (c == '\n') {
            parser->line[parser->pos] = '\0';

            if (parser->pos > 0) {
                char *end = NULL;
                errno = 0;
                double parsed = strtod(parser->line, &end);

                while (end && (*end == ' ' || *end == '\t'))
                    ++end;

                if (errno == 0 && end != parser->line &&
                    end && *end == '\0') {
                    *value = parsed;
                    parser->pos = 0;
                    return 1;
                }
            }

            parser->pos = 0;
            continue;
        }

        if (parser->pos < sizeof(parser->line) - 1)
            parser->line[parser->pos++] = c;
        else
            parser->pos = 0;
    }
}

static void trim_line(char *s)
{
    size_t len = strlen(s);

    while (len > 0 &&
           (s[len - 1] == '\n' || s[len - 1] == '\r' ||
            s[len - 1] == ' '  || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

static void send_control_reply(int fd, const char *reply)
{
    (void)send(fd, reply, strlen(reply), MSG_NOSIGNAL);
}

/* Handle one CLI/control command.
 * Protocol used for now:
 *   UART SHOW
 *   UART BAUD <rate>
 *   UART DEVICE <path>
 *   UART RECONNECT
 */
static void handle_control_command(int client_fd, char *command)
{
    trim_line(command);

    if (strcmp(command, "UART SHOW") == 0) {
        char device[UART_DEVICE_MAX];
        int baud;
        int connected;

        pthread_mutex_lock(&g_uart.lock);
        snprintf(device, sizeof(device), "%s", g_uart.device);
        baud = g_uart.baud;
        connected = g_uart.connected;
        pthread_mutex_unlock(&g_uart.lock);

        char reply[256];
        snprintf(reply, sizeof(reply),
                 "Device: %s\nBaud: %d\nFormat: 8N1\nStatus: %s\n",
                 device, baud, connected ? "connected" : "disconnected");
        send_control_reply(client_fd, reply);
        return;
    }

    int new_baud;
    if (sscanf(command, "UART BAUD %d", &new_baud) == 1) {
        if (baud_to_termios(new_baud) == 0) {
            send_control_reply(client_fd,
                               "ERROR unsupported baud rate\n");
            return;
        }

        pthread_mutex_lock(&g_uart.lock);
        g_uart.baud = new_baud;
        g_uart.reconfigure_requested = 1;
        pthread_mutex_unlock(&g_uart.lock);

        char reply[96];
        snprintf(reply, sizeof(reply),
                 "OK UART baud set to %d\n", new_baud);
        send_control_reply(client_fd, reply);
        return;
    }

    const char prefix[] = "UART DEVICE ";
    if (strncmp(command, prefix, sizeof(prefix) - 1) == 0) {
        const char *new_device = command + sizeof(prefix) - 1;

        if (*new_device == '\0' || strlen(new_device) >= UART_DEVICE_MAX) {
            send_control_reply(client_fd,
                               "ERROR invalid UART device\n");
            return;
        }

        pthread_mutex_lock(&g_uart.lock);
        snprintf(g_uart.device, sizeof(g_uart.device), "%s", new_device);
        g_uart.reconfigure_requested = 1;
        pthread_mutex_unlock(&g_uart.lock);

        char reply[192];
        snprintf(reply, sizeof(reply),
                 "OK UART device set to %s\n", new_device);
        send_control_reply(client_fd, reply);
        return;
    }

    if (strcmp(command, "UART RECONNECT") == 0) {
        pthread_mutex_lock(&g_uart.lock);
        g_uart.reconfigure_requested = 1;
        pthread_mutex_unlock(&g_uart.lock);

        send_control_reply(client_fd,
                           "OK UART reconnect requested\n");
        return;
    }

    send_control_reply(client_fd,
                       "ERROR unknown UART command\n");
}

static void *control_thread(void *arg)
{
    (void)arg;

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("control socket");
        return NULL;
    }

    unlink(CONTROL_SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CONTROL_SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("control bind");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        perror("control listen");
        close(server_fd);
        unlink(CONTROL_SOCKET_PATH);
        return NULL;
    }

    printf("Control socket: %s\n", CONTROL_SOCKET_PATH);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("control accept");
            continue;
        }

        char command[256];
        ssize_t n = recv(client_fd, command, sizeof(command) - 1, 0);

        if (n > 0) {
            command[n] = '\0';
            handle_control_command(client_fd, command);
        }

        close(client_fd);
    }

    return NULL;
}

static void copy_uart_config(char *device, size_t device_size, int *baud)
{
    pthread_mutex_lock(&g_uart.lock);
    snprintf(device, device_size, "%s", g_uart.device);
    *baud = g_uart.baud;
    pthread_mutex_unlock(&g_uart.lock);
}

static void set_uart_connected(int connected)
{
    pthread_mutex_lock(&g_uart.lock);
    g_uart.connected = connected;
    pthread_mutex_unlock(&g_uart.lock);
}

/* The main thread owns uart_fd. The control thread only sets this flag. */
static int take_reconfigure_request(void)
{
    int requested;

    pthread_mutex_lock(&g_uart.lock);
    requested = g_uart.reconfigure_requested;
    g_uart.reconfigure_requested = 0;
    pthread_mutex_unlock(&g_uart.lock);

    return requested;
}

static int reopen_uart(int old_fd, UartParser *parser)
{
    if (old_fd >= 0)
        close(old_fd);

    parser->pos = 0;

    char device[UART_DEVICE_MAX];
    int baud;
    copy_uart_config(device, sizeof(device), &baud);

    int fd = uart_open(device, baud);

    if (fd >= 0) {
        set_uart_connected(1);
        printf("UART opened: %s @ %d baud (8N1)\n", device, baud);
    } else {
        int saved_errno = errno;
        set_uart_connected(0);
        printf("UART unavailable: %s @ %d baud (%s) - using mock data\n",
               device, baud, strerror(saved_errno));
        errno = saved_errno;
    }

    return fd;
}

int main(int argc, char *argv[])
{
    /* Optional startup overrides, useful for PC/QEMU testing. */
    if (argc >= 2) {
        pthread_mutex_lock(&g_uart.lock);
        snprintf(g_uart.device, sizeof(g_uart.device), "%s", argv[1]);
        pthread_mutex_unlock(&g_uart.lock);
    }

    if (argc >= 3) {
        int baud = atoi(argv[2]);
        if (baud_to_termios(baud) == 0) {
            fprintf(stderr, "Unsupported UART baud rate: %d\n", baud);
            return 1;
        }

        pthread_mutex_lock(&g_uart.lock);
        g_uart.baud = baud;
        pthread_mutex_unlock(&g_uart.lock);
    }

    /* Start the independent UART control socket. */
    pthread_t control_tid;
    if (pthread_create(&control_tid, NULL, control_thread, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    pthread_detach(control_tid);

    /* Existing sensor-data socket used by agent. */
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        unlink(SOCKET_PATH);
        return 1;
    }

    char startup_device[UART_DEVICE_MAX];
    int startup_baud;
    copy_uart_config(startup_device, sizeof(startup_device), &startup_baud);

    printf("sensord started\n");
    printf("Sensor socket:  %s\n", SOCKET_PATH);
    printf("UART default:   %s @ %d baud (8N1)\n",
           startup_device, startup_baud);

    UartParser parser = {0};
    int uart_fd = reopen_uart(-1, &parser);
    time_t last_uart_retry = time(NULL);
    double mock_value = MOCK_START_VALUE;

    while (1) {
        printf("Waiting for agent...\n");

        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        printf("Agent connected\n");

        while (1) {
            /* Apply CLI-requested UART changes in the main thread. */
            if (take_reconfigure_request()) {
                uart_fd = reopen_uart(uart_fd, &parser);
                last_uart_retry = time(NULL);
            }

            /* If UART is unavailable, periodically retry it. */
            if (uart_fd < 0) {
                time_t now = time(NULL);

                if (now - last_uart_retry >= UART_RETRY_SEC) {
                    last_uart_retry = now;
                    uart_fd = reopen_uart(-1, &parser);
                }
            }

            double value;
            int have_real_value = 0;

            if (uart_fd >= 0) {
                double uart_value;
                int result = uart_read_value(uart_fd, &parser, &uart_value);

                if (result == 1) {
                    value = uart_value;
                    have_real_value = 1;
                } else if (result < 0) {
                    perror("UART read");
                    close(uart_fd);
                    uart_fd = -1;
                    parser.pos = 0;
                    set_uart_connected(0);
                    last_uart_retry = time(NULL);
                }
            }

            if (!have_real_value) {
                value = mock_value;
                mock_value += MOCK_STEP;
                printf("MOCK: %.2f\n", value);
            } else {
                printf("REAL: %.2f\n", value);
            }

            /* Preserve existing sensord -> agent protocol. */
            char buffer[64];
            int length = snprintf(buffer, sizeof(buffer), "%.2f\n", value);

            if (send(client_fd, buffer, (size_t)length, MSG_NOSIGNAL) < 0) {
                perror("send");
                break;
            }

            sleep(SEND_INTERVAL_SEC);
        }

        printf("Agent disconnected\n");
        close(client_fd);
    }

    return 0;
}
