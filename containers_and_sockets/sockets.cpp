#include <iostream>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <climits>
#include <unistd.h>

#define BUFFER_SIZE 256

using namespace std;

/**
 * uses errno for error identification
 * */
void sys_err_handler(const string& msg){
    cerr << "system error: " << msg << " -> " << strerror(errno) << endl;
    exit(1);
}


/**
 * create socket for both client and host
 * host and client differ at bind & listen (host) vs connect (client)
 * */
int create_socket(string creator, unsigned short port){
    char my_name [ _POSIX_HOST_NAME_MAX + 1];
    int sock_fd;
    struct sockaddr_in sa;
    struct hostent * hp;

    gethostname(my_name, _POSIX_HOST_NAME_MAX);
    hp = gethostbyname(my_name);
    if (hp == nullptr){
        sys_err_handler("gethostbyname");
    }

    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = hp->h_addrtype;

    memcpy(&sa.sin_addr, hp->h_addr, hp->h_length);

    sa.sin_port = htons(port);

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        sys_err_handler("create socket");
    }

    if (creator == "server") {
        if (::bind(sock_fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in)) == -1) {
            close(sock_fd);
            sys_err_handler("bind");
        }
        listen(sock_fd, 5);
    }

    else{
        if (connect(sock_fd, (struct sockaddr *) &sa, sizeof (sa)) == -1){
            close(sock_fd);
            sys_err_handler("connect");
        }
    }

    return sock_fd;
}

/**
 * server accepts connecting requests from clients
 * */
int manage_calls(int host_socket){
    int client_socket;
    if ((client_socket = accept(host_socket, NULL, NULL)) == -1){
        sys_err_handler("accept");
    }
    return client_socket;
}

/**
 * read bytes from socket into buffer ( used by server)
 * */
int read_data(int socket, char* buf, int limit){
    int b_count = 0, b_read = 0;
    while(b_count < limit){
        b_read = read(socket, buf, limit - b_count);
        if (b_read > 0){
            b_count += b_read;
            buf += b_read;
        }
        if (b_read == -1){
            sys_err_handler("read");
        }
    }
    return b_count;
}

/**
 * write bytes from socket into buffer (used by client)
 * */
int write_data(int socket, char* buf, int limit){
    int b_count = 0, b_read = 0;
    while(b_count < limit){
        b_read = write(socket, buf, limit - b_count);
        if (b_read > 0){
            b_count += b_read;
            buf += b_read;
        }
        if (b_read == -1){
            sys_err_handler("write");
        }
    }
    return b_count;
}

int main(int argc, char* argv[]){

    // create socket
    int sock_id = create_socket(argv[1], stoi(argv[2]));


    // case for server
    if (strcmp(argv[1], "server") == 0){
        char buffer[BUFFER_SIZE];
        while (true){
        int client_socket = manage_calls(sock_id);
            read_data(client_socket, buffer, BUFFER_SIZE);
            if (system(buffer) == -1) {
                sys_err_handler("system");
            }
        }
    }

    // case for client
    else{
        write_data(sock_id, argv[3], BUFFER_SIZE);
    }

    return 0;
}
