#include "networking.h"

int main(int argc, char **argv) {

    int server_socket;
    char buffer[BUFFER_SIZE];

    if (argc == 2)
        server_socket = client_setup( argv[1]);
    else
        server_socket = client_setup( TEST_IP );

    //send player name to server
    printf("Enter your name: ");
    fgets(buffer, 15, stdin);
    *strchr(buffer, '\n') = 0;
    write(server_socket, buffer, 15);
    printf("Waiting for game to start...\n");

    while (1) {
        //read update
        read(server_socket, buffer, sizeof(buffer));
        int turn;
        char* data;
        sscanf(buffer, "%d %s", &turn, data);
        printf("Current data: %s\n", data);

        if (turn) {
            printf("Enter new data: ");
            fgets(buffer, sizeof(buffer), stdin);
            *strchr(buffer, '\n') = 0;
            write(server_socket, buffer, sizeof(buffer));
            printf("Sent [%s] to server\n", buffer);
            printf("Waiting for turn...\n");
        }
    }
}
