#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#define AGENT_CONTROL_SOCKET   "/tmp/nour-agent.sock"
#define SENSOR_CONTROL_SOCKET  "/tmp/nour-sensord-ctrl.sock"

#define MAX_COMMAND_LENGTH 256
#define MAX_RESPONSE_LENGTH 512
#define MAX_INPUT_LENGTH 256
#define MAX_ARGS 16


static void print_usage(const char *program)
{
    printf(
        "Usage:\n"
        "  %s status\n"
        "  %s tcp <ip> <port>\n"
        "  %s mqtt <ip> <port> <topic>\n"
        "  %s interval <milliseconds>\n"
        "\n"
        "UART commands:\n"
        "  %s uart show\n"
        "  %s uart baud <baud>\n"
        "  %s uart device <device>\n"
        "  %s uart reconnect\n"
        "\n"
        "Interactive mode:\n"
        "  %s\n",
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program
    );
}


static void print_help(void)
{
    printf(
        "Available commands:\n"
        "  status\n"
        "  tcp <ip> <port>\n"
        "  mqtt <ip> <port> <topic>\n"
        "  interval <milliseconds>\n"
        "\n"
        "UART commands:\n"
        "  uart show\n"
        "  uart baud <baud>\n"
        "  uart device <device>\n"
        "  uart reconnect\n"
        "\n"
        "Other commands:\n"
        "  help\n"
        "  exit\n"
    );
}


/*
 * Convert CLI arguments into the command understood by
 * agent or sensord.
 *
 * argv[] here contains only the actual command words.
 *
 * Example:
 *
 *     argv[0] = "uart"
 *     argv[1] = "baud"
 *     argv[2] = "57600"
 *
 * Returns:
 *     0 = valid command
 *    -1 = invalid command
 */
static int build_command(
    int argc,
    char *argv[],
    char *command,
    size_t command_size,
    const char **control_socket
)
{
    *control_socket = AGENT_CONTROL_SOCKET;


    if (
        argc == 1 &&
        strcmp(argv[0], "status") == 0
    )
    {
        snprintf(
            command,
            command_size,
            "STATUS\n"
        );

        return 0;
    }


    if (
        argc == 3 &&
        strcmp(argv[0], "tcp") == 0
    )
    {
        snprintf(
            command,
            command_size,
            "SET TCP %s %s\n",
            argv[1],
            argv[2]
        );

        return 0;
    }


    if (
        argc == 4 &&
        strcmp(argv[0], "mqtt") == 0
    )
    {
        snprintf(
            command,
            command_size,
            "SET MQTT %s %s %s\n",
            argv[1],
            argv[2],
            argv[3]
        );

        return 0;
    }


    if (
        argc == 2 &&
        strcmp(argv[0], "interval") == 0
    )
    {
        snprintf(
            command,
            command_size,
            "SET INTERVAL %s\n",
            argv[1]
        );

        return 0;
    }


    /*
     * UART commands go to sensord.
     */

    if (
        argc == 2 &&
        strcmp(argv[0], "uart") == 0 &&
        strcmp(argv[1], "show") == 0
    )
    {
        *control_socket = SENSOR_CONTROL_SOCKET;

        snprintf(
            command,
            command_size,
            "UART SHOW\n"
        );

        return 0;
    }


    if (
        argc == 3 &&
        strcmp(argv[0], "uart") == 0 &&
        strcmp(argv[1], "baud") == 0
    )
    {
        *control_socket = SENSOR_CONTROL_SOCKET;

        snprintf(
            command,
            command_size,
            "UART BAUD %s\n",
            argv[2]
        );

        return 0;
    }


    if (
        argc == 3 &&
        strcmp(argv[0], "uart") == 0 &&
        strcmp(argv[1], "device") == 0
    )
    {
        *control_socket = SENSOR_CONTROL_SOCKET;

        snprintf(
            command,
            command_size,
            "UART DEVICE %s\n",
            argv[2]
        );

        return 0;
    }


    if (
        argc == 2 &&
        strcmp(argv[0], "uart") == 0 &&
        strcmp(argv[1], "reconnect") == 0
    )
    {
        *control_socket = SENSOR_CONTROL_SOCKET;

        snprintf(
            command,
            command_size,
            "UART RECONNECT\n"
        );

        return 0;
    }


    return -1;
}


/*
 * Connect to one Unix-domain control socket,
 * send one command, receive one response,
 * then close the connection.
 */
static int send_control_command(
    const char *control_socket,
    const char *command
)
{
    int sock_fd;

    struct sockaddr_un address;

    char response[MAX_RESPONSE_LENGTH];


    sock_fd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0
        );


    if (sock_fd == -1)
    {
        perror("socket");

        return -1;
    }


    memset(
        &address,
        0,
        sizeof(address)
    );


    address.sun_family =
        AF_UNIX;


    snprintf(
        address.sun_path,
        sizeof(address.sun_path),
        "%s",
        control_socket
    );


    if (connect(
            sock_fd,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == -1)
    {
        perror("connect");

        close(sock_fd);

        return -1;
    }


    if (send(
            sock_fd,
            command,
            strlen(command),
            0
        ) == -1)
    {
        perror("send");

        close(sock_fd);

        return -1;
    }


    ssize_t bytes_received =
        recv(
            sock_fd,
            response,
            sizeof(response) - 1,
            0
        );


    if (bytes_received < 0)
    {
        perror("recv");

        close(sock_fd);

        return -1;
    }


    if (bytes_received == 0)
    {
        printf(
            "Control service closed connection\n"
        );

        close(sock_fd);

        return -1;
    }


    response[bytes_received] =
        '\0';


    printf(
        "%s",
        response
    );


    close(sock_fd);


    return 0;
}


/*
 * Split one interactive line into arguments.
 *
 * Example:
 *
 *     "uart baud 57600"
 *
 * becomes:
 *
 *     argv[0] = "uart"
 *     argv[1] = "baud"
 *     argv[2] = "57600"
 */
static int split_line(
    char *line,
    char *argv[],
    int max_args
)
{
    int argc = 0;

    char *token =
        strtok(
            line,
            " \t\r\n"
        );


    while (
        token != NULL &&
        argc < max_args
    )
    {
        argv[argc++] =
            token;


        token =
            strtok(
                NULL,
                " \t\r\n"
            );
    }


    return argc;
}


/*
 * Interactive "nour>" shell.
 */
static int interactive_mode(void)
{
    char line[MAX_INPUT_LENGTH];

    char *args[MAX_ARGS];

    char command[MAX_COMMAND_LENGTH];

    const char *control_socket;


    printf(
        "Nour CLI\n"
        "Type 'help' for commands.\n"
        "\n"
    );


    while (1)
    {
        printf(
            "nour> "
        );


        fflush(stdout);


        if (fgets(
                line,
                sizeof(line),
                stdin
            ) == NULL)
        {
            /*
             * Ctrl+D / EOF
             */
            printf(
                "\nBye.\n"
            );

            break;
        }


        int argc =
            split_line(
                line,
                args,
                MAX_ARGS
            );


        /*
         * Empty line.
         */
        if (argc == 0)
            continue;


        if (
            argc == 1 &&
            strcmp(args[0], "exit") == 0
        )
        {
            printf(
                "Bye.\n"
            );

            break;
        }


        if (
            argc == 1 &&
            strcmp(args[0], "help") == 0
        )
        {
            print_help();

            continue;
        }


        if (build_command(
                argc,
                args,
                command,
                sizeof(command),
                &control_socket
            ) != 0)
        {
            printf(
                "Unknown or invalid command.\n"
                "Type 'help' for commands.\n"
            );

            continue;
        }


        send_control_command(
            control_socket,
            command
        );
    }


    return 0;
}


int main(int argc, char *argv[])
{
    /*
     * No arguments:
     *
     *     ./cli
     *
     * enters interactive mode.
     */
    if (argc == 1)
    {
        return interactive_mode();
    }


    /*
     * One-shot mode:
     *
     *     ./cli status
     *
     *     ./cli uart show
     */
    char command[MAX_COMMAND_LENGTH];

    const char *control_socket;


    if (build_command(
            argc - 1,
            &argv[1],
            command,
            sizeof(command),
            &control_socket
        ) != 0)
    {
        print_usage(
            argv[0]
        );

        return 1;
    }


    if (send_control_command(
            control_socket,
            command
        ) != 0)
    {
        return 1;
    }


    return 0;
}
