#include "networking.h"
#include "cards.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define SEM_KEY 1492
#define SHM_KEY 1776
#define MAX_PLAYERS 6

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

struct player{
  char name[15];
  int hand[20];
};
  
struct game_state {
    struct player players[6]; // TODO: ask user for better name if longer than 13 chars
    int current_player;
    int deck[57]; //52 normal cards + (playercount-1) EK
    int discard[57];
    int cards_left;
    int turn_completed;
    int received_update[6];
    char testing[BUFFER_SIZE];
};


void process(char *s);
void subserver(int from_client, int index);
void setup_shm();
void sighandler(int signum);
void post_setup(int num_players);
void deal_deck(struct game_state*);
void shuffle_deck(struct game_state*);
void init_deck(struct game_state*);
void insert_kitties(struct game_state*);


int subserver_pids[MAX_PLAYERS];

int main() {
    setup_shm();
    signal(SIGINT, sighandler);

    int listen_socket;
    int f;
    listen_socket = server_setup();
    printf("%d\n",listen_socket);

    printf("Press [ENTER] when all players have connected\n");
    if (fork() == 0) {
      getchar();
        //stop accepting connections from socket
        shutdown(listen_socket, SHUT_RD);
        return 0;
    }

    int current_player = 0;
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        subserver_pids[i] = -1;
    }

    while (current_player < MAX_PLAYERS) {
      
	int client_socket = server_connect(listen_socket);
        if (client_socket == -1) {
            printf("Stopped accepting connections\n");
            break;
        }
        f = fork();
        if (f == 0) {
            subserver(client_socket, current_player);
        } else {
            close(client_socket);
            subserver_pids[current_player] = f;
            current_player++;
        }
    }

    post_setup(current_player);
}

void sighandler(int signum) {
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (subserver_pids[i] > 0) {
            printf("Killing subserver #%d with pid %d\n", i, subserver_pids[i]);
            kill(subserver_pids[i], SIGINT);
        }
    }

    int sem_desc = semget(SEM_KEY, 1, 0);
    semctl(sem_desc, 0, IPC_RMID);

    int mem_desc = shmget(SHM_KEY, sizeof(int), 0);
    int shm_val = shmctl(mem_desc, IPC_RMID, 0);
    exit(0);
}

void setup_shm() {
    int sem_desc = semget(SEM_KEY, 1, 0600 | IPC_CREAT | IPC_EXCL);
    if (sem_desc < 0) {
        printf("A server is already running\n");
        exit(0);
    }
    union semun arg;
    arg.val = 1;
    semctl(sem_desc, 0, SETVAL, arg);

    int mem_desc = shmget(SHM_KEY, sizeof(struct game_state), 0600 | IPC_CREAT | IPC_EXCL);
    struct game_state* mem_loc = (struct game_state*) shmat(mem_desc, 0, 0);
    memset(mem_loc, 0, sizeof(struct game_state));
    int i;
    for (i = 0; i < 6; i++) {
      (mem_loc->players[i]).name[0] = 0;
    }
    mem_loc->current_player = -1;
    //printf("starting value: %s\n", mem_loc);
    shmdt(mem_loc);
}

void subserver(int client_socket, int index) {
    printf("subserver created\n");
    int sem_desc = semget(SEM_KEY, 1, 0);
    int mem_desc = shmget(SHM_KEY, sizeof(struct game_state), 0);
    struct game_state* mem_loc = (struct game_state*) shmat(mem_desc, 0, 0);

    char buffer[BUFFER_SIZE];
    memset(buffer,0,BUFFER_SIZE);

    struct sembuf sb;
    sb.sem_op = -1;
    sb.sem_flg = SEM_UNDO;
    sb.sem_num = 0;
    semop(sem_desc, &sb, 1);

    read(client_socket, buffer, 15);
    printf("[subserver %d] player name: %s\n", getpid(), buffer);
    //copy char by char because assigning a pointer directly won't work with shm
    strncpy((mem_loc->players[index]).name, buffer, 15);

    sb.sem_op = 1;
    semop(sem_desc, &sb, 1);

    while (1) {
        sb.sem_op = -1;
        semop(sem_desc, &sb, 1);

        if (mem_loc->received_update[index] == 0) {
            //send update to client, then take input if it's their turn
            //0 means not their turn, 1 means their turn
            if (mem_loc->current_player == index) {
                int all_received_update = 1;
                int i;
                for (i = 0; i < MAX_PLAYERS; i++) {
                    if (mem_loc->received_update[i] == 0 && i != index) {
                        all_received_update = 0;
                        //printf("subserver #%d did not receive the update\n", i);
                    }
                }
		/*
		    i = 0;
  j = 0;
  for(i;i<6;i++){
    if((state->players[i]).name[0]){
      printf("[%s]'s hand: \n", (state->players[i]).name);
      j = 0;
      for(j;j<20;j++){
	printf("[%d] ", (state->players[i]).hand[j]);
      }
      printf("\n");
    }
  }
		*/
                if (all_received_update) {
                    strcpy(buffer, "1 ");
		    int j = 0;
		    for (j;j<20;j++){
		      int card = (mem_loc->players[index]).hand[j];
		      if(card == NONE){
			break;
		      }
		      char temp[256];
		      sprintf(temp,"[%d] ", card);
		      strcat(mem_loc->testing, temp);
		    }
                    strcat(buffer, mem_loc->testing);
                    write(client_socket, buffer, BUFFER_SIZE);
                    read(client_socket, buffer, BUFFER_SIZE);
                    strncpy(mem_loc->testing, buffer, BUFFER_SIZE);
                    mem_loc->turn_completed = 1;
                    mem_loc->received_update[index] = 1;
                }
            } else {
                strcpy(buffer, "0 ");
                strcat(buffer, mem_loc->testing);
                write(client_socket, buffer, BUFFER_SIZE);
                mem_loc->received_update[index] = 1;
            }
        }

        sb.sem_op = 1;
        semop(sem_desc, &sb, 1);
    }

    shmdt(mem_loc);
    close(client_socket);
    exit(0);
}

void post_setup(int num_players) {
    int sem_desc = semget(SEM_KEY, 1, 0);
    int mem_desc = shmget(SHM_KEY, sizeof(struct game_state), 0);
    struct game_state* mem_loc = (struct game_state*) shmat(mem_desc, 0, 0);

    struct sembuf sb;
    sb.sem_op = -1;
    sb.sem_flg = SEM_UNDO;
    sb.sem_num = 0;
    semop(sem_desc, &sb, 1);

    printf("Players: \n");
    for (int i = 0; i < num_players; i++) {
      printf("%s\n", (mem_loc->players[i]).name);
    }
    mem_loc->testing[0] = 0;
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (i < num_players) {
            mem_loc->received_update[i] = 0;
        } else {
            mem_loc->received_update[i] = -1;
        }
    }

    sb.sem_op = 1;
    semop(sem_desc, &sb, 1);

    init_deck(mem_loc);
    shuffle_deck(mem_loc);
    deal_deck(mem_loc);
    insert_kitties(mem_loc);
    shuffle_deck(mem_loc);

    while (1) {
        sb.sem_op = -1;
        semop(sem_desc, &sb, 1);

        int all_received_update = 1;
        for (i = 0; i < num_players; i++) {
            if (mem_loc->received_update[i] == 0) {
                all_received_update = 0;
                //printf("subserver #%d did not receive the update\n", i);
            }
        }

        if (all_received_update) {
            mem_loc->current_player++;
            if (mem_loc->current_player == num_players) {
                mem_loc->current_player = 0;
            }
            //mem_loc->turn_completed = 0;
            for (i = 0; i < num_players; i++) {
                mem_loc->received_update[i] = 0;
            }
	    
	    for(i = 0; i < 57; i++){
	      printf("deck[%d]: %d\n",i,(mem_loc->deck)[i]);
	    }
            printf("Player #%d turn: %s\n", mem_loc->current_player, (mem_loc->players[mem_loc->current_player]).name);
        } else {
            //printf("waiting on someone to get update\n");
        }

        sb.sem_op = 1;
        semop(sem_desc, &sb, 1);
    }
}

void init_deck(struct game_state *state){
  int i = 0;
  int playercount = 0;
  for(i;i<6;i++){
    printf("[%s]\n",(state->players[i]).name);
    if((state->players[i]).name[0]){
      playercount++;
    }
  }
  printf("num players: %d\n",playercount);
  for(i = 0;i<57;i++){
    if(i < 4){
      (state->deck)[i] = ATTACK;
    }
    else if(i < 8){
      (state->deck)[i] = SKIP;
    }
    else if(i < 12){
      (state->deck)[i] = FAVOR;
    }
    else if(i < 16){
      (state->deck)[i] = SHUFFLE;
    }
    else if(i < 21){
      (state->deck)[i] = SEE_THE_FUTURE;
    }
    else if(i < 26){
      (state->deck)[i] = NOPE;
    }
    else if(i < 30){
      (state->deck)[i] = TACOCAT;
    }
    else if(i < 34){
      (state->deck)[i] = WATERMELON_CAT;
    }
    else if(i < 38){
      (state->deck)[i] = POTATO_CAT;
    }
    else if(i < 42){
      (state->deck)[i] = BEARD_CAT;
    }
    else if(i < 46){
      (state->deck)[i] = RAINBOW_CAT;
    }
    else if(i < (52-playercount)){
      (state->deck)[i] = DEFUSE;
    }
    else{
      (state->deck)[i] = NONE;
    }
  }
}

void shuffle_deck(struct game_state *state){
  //Find point of array before "NONE (13)"
  int i = 0;
  int end = 57;
  for(i;i < 57; i++){
    if((state->deck)[i] == 13){
      end = i;
      break;
    }
  }

  //Shuffle by iterating and swapping with random indices
  i = 0;
  srand(time(NULL));
  int randint, temp;
  for(i; i<end; i++){
    randint = rand() % end;
    temp = (state->deck)[i];
    (state->deck)[i] = (state->deck)[randint];
    (state->deck)[randint] = temp;
  }
}

void deal_deck(struct game_state *state){
  //Find point of array before "NONE (13)"
  int i = 0;
  int end = 57;
  for(i;i < 57; i++){
    if((state->deck)[i] == 13){
      end = i;
      break;
    }
  }

  
  i = 0;
  int j;
  for(i;i<6;i++){
    j = 0;
    if((state->players[i]).name[0]){
      (state->players[i]).hand[0] = DEFUSE;
      for(j;j<4;j++){
	(state->players[i]).hand[j+1] = state->deck[end-1];
	state->deck[end-1] = NONE;
	end--;
      }
      (state->players[i]).hand[j+1] = NONE;
    }
  }

  i = 0;
  j = 0;
  for(i;i<6;i++){
    if((state->players[i]).name[0]){
      printf("[%s]'s hand: \n", (state->players[i]).name);
      j = 0;
      for(j;j<20;j++){
	printf("[%d] ", (state->players[i]).hand[j]);
      }
      printf("\n");
    }
  }
}

void insert_kitties(struct game_state* state){
  int i = 0;
  int playercount = 0;
  for(i;i<6;i++){
    //printf("[%s]\n",(state->players[i]).name);
    if((state->players[i]).name[0]){
      playercount++;
    }
  }

  //Find point of array before "NONE (13)"
  i = 0;
  int end = 57;
  for(i;i < 57; i++){
    if((state->deck)[i] == NONE){
      end = i;
      break;
    }
  }


  i = 0;
  for(i;i<playercount-1;i++){
    state->deck[end] = EXPLODING_KITTEN;
    end++;
  }
}



void process(char * s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z')
            *s = ((*s - 'a') + 13) % 26 + 'a';
        else  if (*s >= 'A' && *s <= 'Z')
            *s = ((*s - 'a') + 13) % 26 + 'a';
        s++;
    }
}
