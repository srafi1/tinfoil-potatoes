#include "networking.h"

char * strsepstr(char ** s, char * delim);
int process_input(char * buffer, char ** arr);
int cardtoint(char * cardstring);

int main(int argc, char **argv) {

  int server_socket;
  int hand[57];
  char buffer[BUFFER_SIZE];
  memset(buffer,0,BUFFER_SIZE);

  printf("Enter IP address to connect to (default: 127.0.0.1): ");
  fgets(buffer, BUFFER_SIZE, stdin);

  *strchr(buffer, '\n') = 0;

  if (buffer[0])
    server_socket = client_setup(buffer);
  else
    server_socket = client_setup( TEST_IP );

  //send player name to server
  printf("Enter your name: ");
  fgets(buffer, 50, stdin);
  *strchr(buffer, '\n') = 0;
  write(server_socket, buffer, 50);
  printf("Waiting for game to start...\n");

  //printf("-2");

  while (read(server_socket, buffer, sizeof(buffer))) {
    //read update
    //printf("-1");
    printf("Received [%s] from server\n\n\n", buffer);
    //printf("0");
    int turn;
    char data[256];
    memset(data,0,256);
    sscanf(buffer, "%d", &turn);

    strcpy(data, (buffer + 2));
    printf("%s\n", data);    

    char * line2 = (char*)malloc(200);
    char * line3 = strcpy(line2, buffer);
    char ** arr = (char**)malloc(20* sizeof(char*));
    memset(arr,0,50);

    if(line3){
      //printf("1");
      int i = 0;
      while(line3){
        //printf("2");
        arr[i] = strsepstr(&line3," [");
        i++;
      }
      //printf("3");

      /*      i=0;
              for(i;i<20;i++){
              printf("arr[%d]: %s\n",i,arr[i]);
              }
              printf("\n\n\n\n");
              */
    }


    if (turn) {
      char temp[BUFFER_SIZE];
      memset(temp,0,BUFFER_SIZE);
      memset(buffer,0,BUFFER_SIZE);
      if(turn == 1){
        int card = 0;
        while(!card){
          printf("Your move: ");
          fgets(buffer, sizeof(buffer), stdin);
          if(!(buffer[0] == '\n')){
            *strchr(buffer, '\n') = 0;
            card = process_input(buffer,arr);
          }

          else{
            *strchr(buffer, '\n') = 0;
            card = 99;
            break;
          }

        }

        sprintf(temp,"%d",card);
      }
      else if(turn == 2){
        printf("Pick a deck index: ");
        fgets(buffer, sizeof(buffer), stdin);
        *strchr(buffer, '\n') = 0;
        int index = atoi(buffer);
        sprintf(temp, "%d",index);
      }
      else if(turn == 3){
        printf("Pick a player: ");
        fgets(buffer, sizeof(buffer), stdin);
        *strchr(buffer, '\n') = 0;
        sprintf(temp, "%s", buffer);	
      }
      else if(turn == 4){
        int card = 0;
        while(!card){
          printf("Your sacrifice: ");
          fgets(buffer, sizeof(buffer), stdin);
          if(!(buffer[0] == '\n')){
            *strchr(buffer, '\n') = 0;
            card = process_input(buffer,arr);
          }
        }
        sprintf(temp,"%d", card / -1);
      }
      else if(turn == 9){
        exit(0);
      }

      write(server_socket, temp, sizeof(temp));
      printf("Sent [%s] to server\n", temp);
    }
  }
}

int process_input(char * buffer, char ** arr){
  int index = atoi(buffer);
  int card = 0;
  printf("index: %\n",index);
  if(index < 29 && arr[index]){
    card = cardtoint(arr[index]);
  }
  printf("card at index: %d\n",card);
  if(!card){
    printf("Invalid input. Please try again!\n");
  }
  return card;
}


int cardtoint(char * cardstring){
  printf("==%s==\n",cardstring);
  if(strchr(cardstring, ']')){
    char * bracket = strchr(cardstring, ']');
    if(bracket[1] == ' '){
      bracket[1] = NULL;
    }
  }

  if(strcmp(cardstring,"DEFUSE]") == 0){
    return 1;
  }
  else if(strcmp(cardstring,"ATTACK]") == 0){
    return 2;
  }
  else if(strcmp(cardstring,"SKIP]") == 0){
    return 3;
  }
  else if(strcmp(cardstring,"FAVOR]") == 0){
    return 4;
  }
  else if(strcmp(cardstring,"SHUFFLE]") == 0){
    return 5;
  }
  else if(strcmp(cardstring,"SEE THE FUTURE]") == 0){
    return 6;
  }
  else if(strcmp(cardstring,"REVERSE]") == 0){
    return 7;
  }
  else if(strcmp(cardstring,"TACOCAT]") == 0){
    return 8;
  }
  else if(strcmp(cardstring,"CATTERMELON]") == 0){
    return 9;
  }
  else if(strcmp(cardstring,"POTATO CAT]") == 0){
    return 10;
  }
  else if(strcmp(cardstring,"BEARD CAT]") == 0){
    return 11;
  }
  else if(strcmp(cardstring,"RAINBOW RALPHING CAT]") == 0){
    return 12;
  }
  else{
    return 0;
  }
}


char * strsepstr(char ** s, char * delim){
  char * found = strstr(*s,delim);
  //printf("[%s]",found);
  char * ret = *s;
  if(!found){
    *s = NULL;
    return ret;
  }
  //printf("%s\n",found);
  int i = 0;
  for(i;i<strlen(delim);i++){
    found[i] = NULL;
  }
  i+=1;
  i -= 1;
  *s = found + i;
  return ret;
}
