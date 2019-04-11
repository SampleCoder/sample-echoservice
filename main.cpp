#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <ctime>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <csignal>


#include <vector>
#include <map>


const unsigned short default_port_ = 9951;
const unsigned int buffer_size_ = 66;
static bool shutdown_server_ = false;


static void SigEpipeHandler(int signum) {
    printf("Catch SIGPIPE (%i).\n", signum);
}


bool TrapEpipe() {
    struct sigaction sa{ };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = SigEpipeHandler;
    return sigaction(SIGPIPE, &sa, nullptr) != -1;
}


static void SigintHandler(int signum) {
    printf("Catch SIGINT - server close (%i).\n", signum);
    shutdown_server_ = true;
}


bool TrapIntr() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = SigintHandler;
    return sigaction(SIGINT, &sa, nullptr) != -1;
}


unsigned int CleanupService(std::vector<int> & clients) {
    const char shutdown_warning_message[] = "WARNING: SERVER IS SHUTTING DOWN\n";
    unsigned int count = 0;
    for (int fd : clients) {
        if (write(fd, shutdown_warning_message, sizeof(shutdown_warning_message) - 1) != -1) {
            shutdown(fd, SHUT_RDWR);
            count += 1;
        }
        close(fd);
        printf("%i is closed.\n", fd);
    }
    while (!clients.empty()) clients.pop_back();
    return 0;
}


int ReceiveConnection(int server_fd, long ms_wait, std::map<int, time_t> *timeouts = nullptr) {
    int client_sock = -1;
    fd_set read_fds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = ms_wait;
    FD_ZERO(&read_fds);
    FD_SET(server_fd, &read_fds);

    int n = select(server_fd + 1, &read_fds, nullptr, nullptr, &tv);

    if (n > 0) {
        client_sock = accept(server_fd, nullptr, nullptr);
        if (timeouts && client_sock != -1)
            (*timeouts)[client_sock] = time(nullptr);
    }

    return client_sock;
}


int ProcessClients(const std::vector<int> & target_fds, long ms_wait, std::map<int, time_t> *timeouts = nullptr) {
    int n = 0;
    fd_set readfds;
    FD_ZERO(&readfds);

    int highfd = target_fds[0];
    for (size_t i = 1; i < target_fds.size(); ++ i)
        if (target_fds[i] > highfd)
            highfd = target_fds[i];

    for (auto fd : target_fds) {
        FD_SET(fd, &readfds);
    }
    struct timeval timequant{ };
    timequant.tv_sec = 0;
    timequant.tv_usec = ms_wait;
    n = select(highfd + 1, &readfds, nullptr, nullptr, &timequant);
    if (n > 0) {
        ssize_t bytes_read = 0;
        char read_buffer[buffer_size_];
        for (auto fd : target_fds) {
            if (FD_ISSET(fd, &readfds)) {
                do {
                    bytes_read = read(fd, read_buffer, sizeof(read_buffer) - 1);
                    if (bytes_read > 0) {
                        if (timeouts) {
                            (*timeouts)[fd] = time(nullptr);
                        }
                        read_buffer[bytes_read] = '\0';
                        printf("%s", read_buffer);
                        if (bytes_read < buffer_size_ - 1 || read_buffer[bytes_read - 1] == '\n') {
                            const char ok_msg[] = "OK\n";
                            write(fd, ok_msg, sizeof(ok_msg) - 1);
                            break;
                        }
                    } else {
                        break;
                    }
                } while (true);
            }
        }
    }
    return 0;
}


std::vector<int> PingClients(const std::vector<int> & clients, time_t ping_seed,
        time_t timeout_val = -1, std::map<int, time_t> *timeouts = nullptr) {
    if (time(nullptr) % ping_seed != 0) {
        return clients;
    }
    std::vector<int> active_clients;

    const char PING[] = "PING\n";
    for (int fd : clients) {
        if (write(fd, PING, sizeof(PING) - 1) == -1 && errno == EPIPE) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
            printf("Dropping client socket %i.\n", fd);
        } else {
            bool include = true;
            if (timeouts && timeout_val > 10 && (*timeouts)[fd]) {
                printf("Checking timeout: %li.\n", time(nullptr) - (*timeouts)[fd]);
                if (time(nullptr) - (*timeouts)[fd] > timeout_val) {
                    include = false;
                    const char timeout_message[] = "Disconnect(timeout).\n";
                    printf("Client on socket %i timeout reached.\n", fd);
                    write(fd, timeout_message, sizeof(timeout_message) - 1);
                    shutdown(fd, SHUT_RDWR);
                    close(fd);
                }
            }
            if (include)
                active_clients.push_back(fd);
        }
    }

    return active_clients;
}

int main() {
    int server_socket;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
    perror("[-] socket creation failed");
    return 1;
    }

    int option = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) == -1) {
        perror("[warn] cannot setsockopt");
    }

    struct sockaddr_in service_inet_addr{ };

    service_inet_addr.sin_family = AF_INET;
    service_inet_addr.sin_port = htons(default_port_);
    service_inet_addr.sin_addr.s_addr = INADDR_ANY;


    if (bind(server_socket,
            reinterpret_cast<const sockaddr *>(&service_inet_addr),
            sizeof(service_inet_addr)) == -1) {
        perror("[-] unable to bind");
        return 2;
    }

    if (listen(server_socket, 5) == -1) {
        perror("[-] listen error");
        return 3;
    }
    printf("Service is ready on socket %i at port %i.\n", server_socket, default_port_);

    if (TrapIntr()) {
        printf("[+] Ctrl-C is trapped now.\n");
    }

    if (TrapEpipe())
        printf("[+] EPIPE is trapped now.\n");

    std::vector<int> clients_sockets;
    std::map<int, time_t> client_timeouts;
    int client_socket;
    for (;;) {
        client_socket = ReceiveConnection(server_socket, 500000L, &client_timeouts);
        if (client_socket != -1) {
            printf("Adding client on socket %i.\n", client_socket);
            clients_sockets.push_back(client_socket);
        }

        // If no input within 4s then proceed.
        if (!clients_sockets.empty())
            ProcessClients(clients_sockets, 400000L, &client_timeouts);
        clients_sockets = PingClients(clients_sockets, 5, 15, &client_timeouts);
        if (shutdown_server_) {
            CleanupService(clients_sockets);
            break;
        }
    }

    shutdown(server_socket, SHUT_RD);
    close(server_socket);
    return 0;
}
