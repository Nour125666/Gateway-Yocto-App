#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>
#include <sstream>
#include <chrono>
#include <cstdint>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <cstring>
#include <cstdlib>

#include <mqtt/client.h>

#define SENSOR_SOCKET  "/tmp/nour-sensor.sock"
#define CONTROL_SOCKET "/tmp/nour-agent.sock"


/* =========================================================
   SHARED CONFIGURATION
   ========================================================= */

struct AgentConfig
{
    std::string mode;
    std::string host;
    int port;
    std::string topic;

    int interval_ms;

    /*
     * Incremented only when TCP/MQTT configuration changes.
     * This causes the current transport connection to restart.
     */
    unsigned long transport_version;

    /*
     * Incremented when interval changes.
     * This does NOT reconnect TCP/MQTT.
     */
    unsigned long interval_version;
};

AgentConfig config;


/* =========================================================
   SHARED SENSOR DATA
   ========================================================= */

std::string sensor_data;

uint64_t sensor_sequence = 0;

std::mutex state_mutex;

std::condition_variable state_changed;

std::atomic<bool> running{true};


/* =========================================================
   VALIDATION
   ========================================================= */

bool parse_port(
    const std::string& text,
    int& port
)
{
    if (text.empty())
        return false;

    char *end;

    long value =
        std::strtol(
            text.c_str(),
            &end,
            10
        );

    if (*end != '\0')
        return false;

    if (value <= 0 || value > 65535)
        return false;

    port =
        static_cast<int>(value);

    return true;
}


bool parse_interval(
    const std::string& text,
    int& interval
)
{
    if (text.empty())
        return false;

    char *end;

    long value =
        std::strtol(
            text.c_str(),
            &end,
            10
        );

    if (*end != '\0')
        return false;

    if (value < 100 || value > 60000)
        return false;

    interval =
        static_cast<int>(value);

    return true;
}


/* =========================================================
   SENSOR THREAD
   ========================================================= */

void sensor_thread()
{
    int sensor_fd;

    struct sockaddr_un address;

    char buffer[64];


    sensor_fd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0
        );


    if (sensor_fd == -1)
    {
        perror("sensor socket");

        running = false;
        state_changed.notify_all();

        return;
    }


    memset(
        &address,
        0,
        sizeof(address)
    );


    address.sun_family =
        AF_UNIX;


    strcpy(
        address.sun_path,
        SENSOR_SOCKET
    );


    if (connect(
            sensor_fd,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == -1)
    {
        perror("sensor connect");

        close(sensor_fd);

        running = false;
        state_changed.notify_all();

        return;
    }


    std::cout
        << "Sensor thread connected to sensord"
        << std::endl;


    while (running)
    {
        ssize_t bytes_received =
            recv(
                sensor_fd,
                buffer,
                sizeof(buffer),
                0
            );


        if (bytes_received <= 0)
        {
            std::cout
                << "sensord disconnected"
                << std::endl;

            running = false;

            state_changed.notify_all();

            break;
        }


        {
            std::lock_guard<std::mutex> lock(
                state_mutex
            );


            sensor_data =
                std::string(
                    buffer,
                    bytes_received
                );


            sensor_sequence++;
        }


        state_changed.notify_all();
    }


    close(sensor_fd);
}


/* =========================================================
   WAIT BEFORE NEXT TRANSMISSION

   Important:

   Sensor data may arrive every second, but this function
   keeps the configured network transmission interval.

   Example:

   sensor:   1 second
   interval: 3 seconds

   Agent sends only the newest value every ~3 seconds.
   ========================================================= */

bool wait_for_next_send(
    unsigned long transport_version,
    std::chrono::steady_clock::time_point last_send
)
{
    while (running)
    {
        std::unique_lock<std::mutex> lock(
            state_mutex
        );


        /*
         * Transport changed?
         *
         * Example:
         *
         * TCP -> MQTT
         */
        if (
            config.transport_version !=
            transport_version
        )
        {
            return false;
        }


        int interval =
            config.interval_ms;


        unsigned long current_interval_version =
            config.interval_version;


        auto deadline =
            last_send +
            std::chrono::milliseconds(
                interval
            );


        /*
         * Interval already passed.
         */
        if (
            std::chrono::steady_clock::now()
            >= deadline
        )
        {
            return true;
        }


        state_changed.wait_until(
            lock,
            deadline,
            [&] {
                return
                    !running ||

                    config.transport_version !=
                        transport_version ||

                    config.interval_version !=
                        current_interval_version;
            }
        );


        if (!running)
            return false;


        if (
            config.transport_version !=
            transport_version
        )
        {
            return false;
        }


        /*
         * Interval changed while waiting.
         *
         * Recalculate deadline using the
         * new interval.
         */
        if (
            config.interval_version !=
            current_interval_version
        )
        {
            continue;
        }


        /*
         * Normal timeout:
         * transmission interval expired.
         */
        if (
            std::chrono::steady_clock::now()
            >= deadline
        )
        {
            return true;
        }
    }


    return false;
}


/* =========================================================
   TCP SEND ALL

   send() is allowed to send fewer bytes than requested.
   This helper ensures the whole sensor message is sent.
   ========================================================= */

bool tcp_send_all(
    int fd,
    const std::string& data
)
{
    size_t total_sent = 0;


    while (
        total_sent <
        data.size()
    )
    {
        ssize_t sent =
            send(
                fd,
                data.data() + total_sent,
                data.size() - total_sent,
                MSG_NOSIGNAL
            );


        if (sent <= 0)
        {
            return false;
        }


        total_sent +=
            static_cast<size_t>(sent);
    }


    return true;
}


/* =========================================================
   RETRY DELAY

   Used when TCP/MQTT connection fails.

   A CLI transport change wakes this immediately.
   ========================================================= */

void wait_before_retry(
    unsigned long transport_version
)
{
    std::unique_lock<std::mutex> lock(
        state_mutex
    );


    state_changed.wait_for(
        lock,
        std::chrono::seconds(1),
        [&] {
            return
                !running ||

                config.transport_version !=
                    transport_version;
        }
    );
}


/* =========================================================
   TCP SESSION

   Only called by transport_thread().
   ========================================================= */

void run_tcp(
    const AgentConfig& local_config,
    uint64_t& last_sequence
)
{
    int tcp_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );


    if (tcp_fd == -1)
    {
        perror("tcp socket");

        wait_before_retry(
            local_config.transport_version
        );

        return;
    }


    struct sockaddr_in address;


    memset(
        &address,
        0,
        sizeof(address)
    );


    address.sin_family =
        AF_INET;


    address.sin_port =
        htons(
            local_config.port
        );


    if (inet_pton(
            AF_INET,
            local_config.host.c_str(),
            &address.sin_addr
        ) != 1)
    {
        std::cerr
            << "TCP: invalid IP address"
            << std::endl;


        close(tcp_fd);


        wait_before_retry(
            local_config.transport_version
        );


        return;
    }


    if (connect(
            tcp_fd,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == -1)
    {
        perror("tcp connect");


        close(tcp_fd);


        wait_before_retry(
            local_config.transport_version
        );


        return;
    }


    std::cout
        << "TCP connected to "
        << local_config.host
        << ":"
        << local_config.port
        << std::endl;


    while (running)
    {
        std::string data;


        /* -----------------------------------------
           Wait for new sensor data or mode change
           ----------------------------------------- */

        {
            std::unique_lock<std::mutex> lock(
                state_mutex
            );


            state_changed.wait(
                lock,
                [&] {
                    return
                        !running ||

                        config.transport_version !=
                            local_config.transport_version ||

                        sensor_sequence >
                            last_sequence;
                }
            );


            if (!running)
                break;


            /*
             * CLI changed transport/configuration.
             */
            if (
                config.transport_version !=
                local_config.transport_version
            )
            {
                break;
            }


            /*
             * Copy newest sensor value.
             */
            data =
                sensor_data;


            last_sequence =
                sensor_sequence;
        }


        /* -----------------------------------------
           Send TCP data
           ----------------------------------------- */

        if (
            !tcp_send_all(
                tcp_fd,
                data
            )
        )
        {
            perror("tcp send");

            break;
        }


        std::cout
            << "TCP -> "
            << data;


        auto last_send =
            std::chrono::steady_clock::now();


        /*
         * Enforce network transmission interval.
         *
         * If CLI switches to MQTT while we're waiting,
         * this wakes immediately and returns false.
         */
        if (
            !wait_for_next_send(
                local_config.transport_version,
                last_send
            )
        )
        {
            break;
        }
    }


    /*
     * IMPORTANT:
     *
     * We close the TCP connection BEFORE transport_thread
     * can start MQTT.
     */
    shutdown(
        tcp_fd,
        SHUT_RDWR
    );


    close(
        tcp_fd
    );


    std::cout
        << "TCP disconnected"
        << std::endl;
}


/* =========================================================
   MQTT SESSION

   Only called by transport_thread().
   ========================================================= */

void run_mqtt(
    const AgentConfig& local_config,
    uint64_t& last_sequence
)
{
    std::string broker =
        "tcp://" +
        local_config.host +
        ":" +
        std::to_string(
            local_config.port
        );


    try
    {
        mqtt::client client(
            broker,
            "nour-agent"
        );


        mqtt::connect_options options;


        options.set_clean_session(
            true
        );


        options.set_keep_alive_interval(
            20
        );


        client.connect(
            options
        );


        std::cout
            << "MQTT connected to "
            << broker
            << std::endl;


        while (running)
        {
            std::string data;


            /* -----------------------------------------
               Wait for sensor data or mode change
               ----------------------------------------- */

            {
                std::unique_lock<std::mutex> lock(
                    state_mutex
                );


                state_changed.wait(
                    lock,
                    [&] {
                        return
                            !running ||

                            config.transport_version !=
                                local_config.transport_version ||

                            sensor_sequence >
                                last_sequence;
                    }
                );


                if (!running)
                    break;


                if (
                    config.transport_version !=
                    local_config.transport_version
                )
                {
                    break;
                }


                data =
                    sensor_data;


                last_sequence =
                    sensor_sequence;
            }


            /* -----------------------------------------
               Publish MQTT
               ----------------------------------------- */

            auto message =
                mqtt::make_message(
                    local_config.topic,
                    data
                );


            message->set_qos(
                1
            );


            client.publish(
                message
            );


            std::cout
                << "MQTT -> "
                << local_config.topic
                << " : "
                << data;


            auto last_send =
                std::chrono::steady_clock::now();


            if (
                !wait_for_next_send(
                    local_config.transport_version,
                    last_send
                )
            )
            {
                break;
            }
        }


        /*
         * Disconnect MQTT BEFORE transport_thread
         * moves to TCP.
         */
        if (
            client.is_connected()
        )
        {
            client.disconnect();
        }


        std::cout
            << "MQTT disconnected"
            << std::endl;
    }


    catch (
        const mqtt::exception& error
    )
    {
        std::cerr
            << "MQTT error: "
            << error.what()
            << std::endl;


        wait_before_retry(
            local_config.transport_version
        );
    }
}


/* =========================================================
   TRANSPORT THREAD

   SINGLE NETWORK THREAD

                  ┌── TCP
   transport ─────┤
                  └── MQTT

   Never both simultaneously.
   ========================================================= */

void transport_thread()
{
    uint64_t last_sequence = 0;


    while (running)
    {
        AgentConfig local_config;


        /*
         * Take a snapshot of current configuration.
         */
        {
            std::unique_lock<std::mutex> lock(
                state_mutex
            );


            state_changed.wait(
                lock,
                [] {
                    return
                        !running ||

                        config.mode == "tcp" ||

                        config.mode == "mqtt";
                }
            );


            if (!running)
                break;


            local_config =
                config;
        }


        /* -----------------------------------------
           Run selected transport
           ----------------------------------------- */

        if (
            local_config.mode ==
            "tcp"
        )
        {
            run_tcp(
                local_config,
                last_sequence
            );
        }


        else if (
            local_config.mode ==
            "mqtt"
        )
        {
            run_mqtt(
                local_config,
                last_sequence
            );
        }


        /*
         * When run_tcp() or run_mqtt() returns,
         * loop again and read the latest configuration.
         */
    }
}


/* =========================================================
   CONTROL THREAD
   ========================================================= */

void control_thread()
{
    int server_fd;
    int client_fd;

    struct sockaddr_un address;

    char buffer[256];


    server_fd =
        socket(
            AF_UNIX,
            SOCK_STREAM,
            0
        );


    if (server_fd == -1)
    {
        perror("control socket");

        return;
    }


    memset(
        &address,
        0,
        sizeof(address)
    );


    address.sun_family =
        AF_UNIX;


    strcpy(
        address.sun_path,
        CONTROL_SOCKET
    );


    unlink(
        CONTROL_SOCKET
    );


    if (bind(
            server_fd,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == -1)
    {
        perror("control bind");

        close(server_fd);

        return;
    }


    if (listen(
            server_fd,
            5
        ) == -1)
    {
        perror("control listen");

        close(server_fd);

        unlink(CONTROL_SOCKET);

        return;
    }


    std::cout
        << "Control thread listening on "
        << CONTROL_SOCKET
        << std::endl;


    while (running)
    {
        client_fd =
            accept(
                server_fd,
                NULL,
                NULL
            );


        if (client_fd == -1)
        {
            continue;
        }


        ssize_t bytes_received =
            recv(
                client_fd,
                buffer,
                sizeof(buffer) - 1,
                0
            );


        if (bytes_received > 0)
        {
            buffer[bytes_received] =
                '\0';


            std::string command(
                buffer
            );


            while (
                !command.empty() &&
                (
                    command.back() == '\n' ||
                    command.back() == '\r'
                )
            )
            {
                command.pop_back();
            }


            std::cout
                << "CLI command: "
                << command
                << std::endl;


            std::istringstream stream(
                command
            );


            std::string action;


            stream >>
                action;


            /* =============================================
               STATUS
               ============================================= */

            if (
                action ==
                "STATUS"
            )
            {
                AgentConfig local_config;


                {
                    std::lock_guard<std::mutex> lock(
                        state_mutex
                    );


                    local_config =
                        config;
                }


                std::string response;


                response +=
                    "mode=" +
                    local_config.mode +
                    "\n";


                response +=
                    "host=" +
                    local_config.host +
                    "\n";


                response +=
                    "port=" +
                    std::to_string(
                        local_config.port
                    ) +
                    "\n";


                response +=
                    "interval_ms=" +
                    std::to_string(
                        local_config.interval_ms
                    ) +
                    "\n";


                if (
                    local_config.mode ==
                    "mqtt"
                )
                {
                    response +=
                        "topic=" +
                        local_config.topic +
                        "\n";
                }


                send(
                    client_fd,
                    response.c_str(),
                    response.size(),
                    MSG_NOSIGNAL
                );
            }


            /* =============================================
               SET
               ============================================= */

            else if (
                action ==
                "SET"
            )
            {
                std::string parameter;


                stream >>
                    parameter;


                /* -----------------------------------------
                   SET TCP
                   ----------------------------------------- */

                if (
                    parameter ==
                    "TCP"
                )
                {
                    std::string host;
                    std::string port_text;

                    int port;


                    stream >>
                        host >>
                        port_text;


                    if (
                        host.empty() ||
                        !parse_port(
                            port_text,
                            port
                        )
                    )
                    {
                        const char *response =
                            "ERROR invalid TCP configuration\n";


                        send(
                            client_fd,
                            response,
                            strlen(response),
                            MSG_NOSIGNAL
                        );
                    }


                    else
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                state_mutex
                            );


                            config.mode =
                                "tcp";


                            config.host =
                                host;


                            config.port =
                                port;


                            /*
                             * Force transport_thread to close
                             * current transport and restart.
                             */
                            config.transport_version++;
                        }


                        state_changed.notify_all();


                        const char *response =
                            "OK TCP configured\n";


                        send(
                            client_fd,
                            response,
                            strlen(response),
                            MSG_NOSIGNAL
                        );
                    }
                }


                /* -----------------------------------------
                   SET MQTT
                   ----------------------------------------- */

                else if (
                    parameter ==
                    "MQTT"
                )
                {
                    std::string host;
                    std::string port_text;
                    std::string topic;

                    int port;


                    stream >>
                        host >>
                        port_text >>
                        topic;


                    if (
                        host.empty() ||
                        topic.empty() ||
                        !parse_port(
                            port_text,
                            port
                        )
                    )
                    {
                        const char *response =
                            "ERROR invalid MQTT configuration\n";


                        send(
                            client_fd,
                            response,
                            strlen(response),
                            MSG_NOSIGNAL
                        );
                    }


                    else
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                state_mutex
                            );


                            config.mode =
                                "mqtt";


                            config.host =
                                host;


                            config.port =
                                port;


                            config.topic =
                                topic;


                            config.transport_version++;
                        }


                        state_changed.notify_all();


                        const char *response =
                            "OK MQTT configured\n";


                        send(
                            client_fd,
                            response,
                            strlen(response),
                            MSG_NOSIGNAL
                        );
                    }
                }


                /* -----------------------------------------
                   SET INTERVAL
                   ----------------------------------------- */

                else if (
                    parameter ==
                    "INTERVAL"
                )
                {
                    std::string interval_text;

                    int interval;


                    stream >>
                        interval_text;


                    if (
                        !parse_interval(
                            interval_text,
                            interval
                        )
                    )
                    {
                        const char *response =
                            "ERROR interval must be 100-60000 ms\n";


                        send(
                            client_fd,
                            response,
                            strlen(response),
                            MSG_NOSIGNAL
                        );
                    }


                    else
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                state_mutex
                            );


                            config.interval_ms =
                                interval;


                            /*
                             * Interval changes do NOT restart
                             * TCP/MQTT.
                             */
                            config.interval_version++;
                        }


                        state_changed.notify_all();


                        std::string response =
                            "OK interval=" +
                            std::to_string(interval) +
                            " ms\n";


                        send(
                            client_fd,
                            response.c_str(),
                            response.size(),
                            MSG_NOSIGNAL
                        );
                    }
                }


                else
                {
                    const char *response =
                        "ERROR unknown SET parameter\n";


                    send(
                        client_fd,
                        response,
                        strlen(response),
                        MSG_NOSIGNAL
                    );
                }
            }


            else
            {
                const char *response =
                    "ERROR unknown command\n";


                send(
                    client_fd,
                    response,
                    strlen(response),
                    MSG_NOSIGNAL
                );
            }
        }


        close(
            client_fd
        );
    }


    close(
        server_fd
    );


    unlink(
        CONTROL_SOCKET
    );
}


/* =========================================================
   MAIN
   ========================================================= */

int main(
    int argc,
    char *argv[]
)
{
    if (argc < 4)
    {
        std::cerr
            << "Usage:\n"
            << "  "
            << argv[0]
            << " tcp <ip> <port>\n"
            << "  "
            << argv[0]
            << " mqtt <ip> <port> <topic>\n";


        return 1;
    }


    std::string mode =
        argv[1];


    std::string host =
        argv[2];


    int port;


    if (
        !parse_port(
            argv[3],
            port
        )
    )
    {
        std::cerr
            << "Invalid port"
            << std::endl;


        return 1;
    }


    if (
        mode != "tcp" &&
        mode != "mqtt"
    )
    {
        std::cerr
            << "Transport must be tcp or mqtt"
            << std::endl;


        return 1;
    }


    if (
        mode == "tcp" &&
        argc != 4
    )
    {
        std::cerr
            << "Usage: "
            << argv[0]
            << " tcp <ip> <port>"
            << std::endl;


        return 1;
    }


    if (
        mode == "mqtt" &&
        argc != 5
    )
    {
        std::cerr
            << "Usage: "
            << argv[0]
            << " mqtt <ip> <port> <topic>"
            << std::endl;


        return 1;
    }


    /* Initial configuration */

    config.mode =
        mode;


    config.host =
        host;


    config.port =
        port;


    config.topic =
        (
            mode == "mqtt"
            ? argv[4]
            : "-"
        );


    config.interval_ms =
        1000;


    config.transport_version =
        1;


    config.interval_version =
        1;


    /*
     * THREE threads only.
     */

    std::thread sensor(
        sensor_thread
    );


    std::thread transport(
        transport_thread
    );


    std::thread control(
        control_thread
    );


    sensor.join();

    transport.join();

    control.join();


    std::cout
        << "nour-agent stopped"
        << std::endl;


    return 0;
}
