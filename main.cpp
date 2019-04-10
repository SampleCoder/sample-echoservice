#include <cstdio>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>


const unsigned short default_port_ = 9951;


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

    fd_set server_socket_set;
    struct timeval time_slice{ };

    int select_status = 0;
    while (true) {

        time_slice.tv_sec = 6;
        time_slice.tv_usec = 0;

        FD_ZERO(&server_socket_set);
        FD_SET(server_socket, &server_socket_set);

        select_status =
                select(server_socket + 1, &server_socket_set, nullptr, nullptr, &time_slice);

        if (select_status == -1) {
            perror("[-] select failure");
            return 12;
        }
        if (select_status == 0) {
            printf("No clients within 6 secs.\n");
        } else {
            if (FD_ISSET(server_socket, &server_socket_set)) {
                printf("NOTE: Fd Isset true.\n");
            }
            break;
        }
    }

    int client_socket = accept(server_socket, nullptr, nullptr);
    if (client_socket == -1) {
        perror("[-] unable to accept incoming connection");
        return 4;
    }

    long n;
    char buf[51];
    const char msg[] = "OK\n";
    while (true) {

        n = read(client_socket, buf, sizeof(buf) - 1);

        if (n == -1) {
            perror("[!] read error");
            break;
        }

        if (n == 0) break;

        buf[n] = '\0';
        printf("%s", buf);
        n = write(client_socket, msg, sizeof(msg) - 1);
        if (n == -1) {
            perror("[!] write error");
        } else {
            printf("%li bytes sent.\n", n);
        }
    }

    close(server_socket);
    return 0;
}
