#include "networking.h"

int main(int argc, char **argv) {

  int server_socket;
  int hand[57];
  char buffer[BUFFER_SIZE];
  memset(buffer,0,BUFFER_SIZE);

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
    //printf("Received [%s] from server\n", buffer);
    int turn;
    char data[256];
    memset(data,0,256);
    sscanf(buffer, "%d  %[^\t\n]s", &turn, data);
    printf("%s\n", data);

    if (turn) {
      printf("Your move: ");
      fgets(buffer, sizeof(buffer), stdin);
      *strchr(buffer, '\n') = 0;
      write(server_socket, buffer, sizeof(buffer));
      //printf("Sent [%s] to server\n", buffer);
    }
  }
}
