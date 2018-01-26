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
  struct semid_ds *bsuf;
  unsigned short *array;
  struct seminfo *__buf;
};

struct player{
  char name[50];
  int hand[20];
  int attacked;
  int alive;
};
  
struct game_state {
  struct player players[6];
  int current_player;
  int deck[57]; //52 normal cards + (playercount-1) EK
  int discard[57];
  int cards_left;
  int turn_completed;
  int received_update[6];
  char testing[BUFFER_SIZE];
  int reversed;
  int ended;
  int favor;
  int benefactor;
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
void process_action(int client_socket, struct game_state *state, char * buffer, int playerindex, int new);
char * draw(int client_socket, struct game_state *state, int playerindex);
char * cardtotext(int cardid);
char * thefuture();


int subserver_pids[MAX_PLAYERS];

int main() {
  setup_shm();
  signal(SIGINT, sighandler);

  int listen_socket;
  int f;
  listen_socket = server_setup();
  //printf("%d\n",listen_socket);

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
      if(signum == SIGINT){
	kill(subserver_pids[i], SIGINT);
      }
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
    //(mem_loc->players[i]).name[0] = 0;
    mem_loc->players[i].alive = 1;
  }
  for(i=0; i<6; i++){
    (mem_loc->players[i]).hand[0] = NONE;
  }
  mem_loc->current_player = -1;
  mem_loc->reversed = 1;
  mem_loc->ended = 0;
  mem_loc->favor = -1;
  mem_loc->benefactor = -1;
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

  read(client_socket, buffer, 50);
  printf("[subserver %d] player name: %s\n", getpid(), buffer);
  //copy char by char because assigning a pointer directly won't work with shm
  strncpy((mem_loc->players[index]).name, buffer, 50);

  sb.sem_op = 1;
  semop(sem_desc, &sb, 1);

  while (1) {
    //printf("sub");
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
	    //printf("sub: subserver #%d did not receive the update\n", i);
	  }
	}

	if (all_received_update) {
	  if(mem_loc->favor != -1 && index == mem_loc->favor){
	    strcpy(buffer, "4 ");
	  }
	  else{
	    strcpy(buffer, "1 ");
	  }

	  char tmp[100];
	  //	  sprintf(tmp, "attacked: %d\n",mem_loc->players[index].attacked);
	  //strcat(buffer, tmp);
	  //strcat(buffer, "initial ");
	  int j = 0;
	  //memset(mem_loc->testing,0,BUFFER_SIZE);
	  strcat(buffer, "Your hand: ");
	  for (j;j<20;j++){
	    int card = (mem_loc->players[index]).hand[j];
	    if(card == NONE){
	      break;
	    }
	    char temp[256];
	    memset(temp,0,256);
	    sprintf(temp,"[%s] ", cardtotext(card));
	    strcat(buffer, temp);
	  }
	  //printf("[%s]\n", mem_loc->testing);
	  strcat(buffer, "\n");
	  if(index != mem_loc->current_player - mem_loc->reversed){
	    strcat(buffer, mem_loc->testing);
	  }
	  //printf("ended: %d\n",mem_loc->ended);
	  if(mem_loc->ended){
	    memset(buffer,0,BUFFER_SIZE);
	    strcat(buffer,"9 ");
	    if(mem_loc->players[index].alive){
	      strcat(buffer,"YOU WON, CONGRATULATIONS!!! :) \n");
	    }
	    else{
	      int i = 0;
	      for(i;i<6;i++){
		if(mem_loc->players[i].alive){
		  char tomp[70];
		  sprintf(tomp,"%s has won, and you lost :( \n",mem_loc->players[i].name);
		  strcat(buffer, tomp);
		  break;
		}
	      }
	    }
	    write(client_socket, buffer, BUFFER_SIZE);
	    mem_loc->received_update[index] = 1;
	    exit(0);
	  }
	  else if(mem_loc->players[index].alive){
	    if(index == mem_loc->favor){
	      strcat(buffer,"You have been asked for a favor!\n");
	      strcat(buffer,"Choose a card to relinquish!\n");
	    }
	    write(client_socket, buffer, BUFFER_SIZE);
	    memset(buffer,0,BUFFER_SIZE);
	    //printf("TEST");
	    read(client_socket, buffer, BUFFER_SIZE);
	    //printf("Received [%s] from client",buffer);
	    process_action(client_socket,mem_loc,buffer,index,1);
	    //printf("my turn and received update");
	  }

	  mem_loc->turn_completed = 1;
	  mem_loc->received_update[index] = 1;
	}
      }
      else {
	  strcpy(buffer, "0 ");
	  char tmp[100];
	  //sprintf(tmp, "attacked: %d\n",mem_loc->players[index].attacked);
	  //strcat(buffer, tmp);
	  //strcat(buffer, "not your turn");
	  /*	int j = 0;
	  //memset(mem_loc->testing,0,BUFFER_SIZE);
	  strcat(buffer, "Your hand: ");
	  for (j;j<20;j++){
	  int card = (mem_loc->players[index]).hand[j];
	  if(card == NONE){
	  break;
	  }
	  char temp[256];
	  memset(temp,0,256);
	  sprintf(temp,"[%s] ", cardtotext(card));
	  strcat(buffer, temp);
	  }
	  strcat(buffer, "\n");*/
	
	  //printf("[%s]\n", mem_loc->testing);
	  if(index != mem_loc->current_player - mem_loc->reversed){
	    strcat(buffer, mem_loc->testing);
	  }
	  //printf("ended: %d\n",mem_loc->ended);
	  if(mem_loc->ended){
	    memset(buffer,0,BUFFER_SIZE);
	    strcat(buffer,"9 ");
	    if(mem_loc->players[index].alive){
	      strcat(buffer,"YOU WON, CONGRATULATIONS!!! :) \n");
	    }
	    else{
	      int i = 0;
	      for(i;i<6;i++){
		if(mem_loc->players[i].alive){
		  char tomp[70];
		  sprintf(tomp,"%s has won, and you lost :( \n",mem_loc->players[i].name);
		  strcat(buffer, tomp);
		  break;
		}
	      }
	    }


	    write(client_socket, buffer, BUFFER_SIZE);
	    memset(buffer,0,BUFFER_SIZE);
	    
	    mem_loc->received_update[index] = 1;
	  
	  
	  exit(0);
	}

	write(client_socket, buffer, BUFFER_SIZE);
	mem_loc->received_update[index] = 1;
	//printf("not my turn but received update");
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
	//printf("post: subserver #%d did not receive the update\n", i);
      }
    }


    if (all_received_update) {


      if(mem_loc->benefactor != -1){	
	mem_loc->current_player = mem_loc->benefactor;

	if(mem_loc->favor != -1){
	  mem_loc->current_player = mem_loc->favor;
	}
	else{
	  mem_loc->benefactor = -1;
	}
      }



      else {
	mem_loc->current_player+= mem_loc->reversed;
	if (mem_loc->current_player == num_players) {
	  mem_loc->current_player = 0;
	}
	
	if(mem_loc->current_player == -1){
	  mem_loc->current_player = num_players - 1;
	}
	
      }
      
      
      //mem_loc->turn_completed = 0;
      int count = 0;
      for (i = 0; i < num_players; i++) {
	mem_loc->received_update[i] = 0;
	if(mem_loc->players[i].alive){
	  count++;
	}
      }
      /*	    
      for(i = 0; i < 57; i++){
	printf("deck[%d]: %s\n",i,cardtotext((mem_loc->deck)[i]));
      }
      */
      printf("players alive: %d\n", count);
      /*printf("favor: %d\n", mem_loc->favor);
	printf("benefactor: %d\n", mem_loc->benefactor);*/
      if(count==1){
	printf("%s has won the game! :D\n", mem_loc->players[mem_loc->current_player].name);
	mem_loc->ended = 1;
	sleep(1);
	sighandler(SIGTERM);
      }
      printf("Player #%d turn: %s\n", mem_loc->current_player, (mem_loc->players[mem_loc->current_player]).name);
      
    }
    else {
      //printf("waiting on someone to get update\n");
    }
    sb.sem_op = 1;
    semop(sem_desc, &sb, 1);
  }
}

void process_action(int client_socket, struct game_state *state, char * buffer, int playerindex, int new){

  //printf("made it to processing\n");
  int input = atoi(buffer);
  //printf("input: %d\n",input);
  memset(buffer,0,BUFFER_SIZE);
  char output[BUFFER_SIZE];
  memset(output,0,BUFFER_SIZE);
  char message[BUFFER_SIZE];
  memset(message,0,BUFFER_SIZE);
  
  int x = 0;
  for(x;x<6;x++){
    if(state->players[x].name[0] == 0){
      break;
    }
  }

  //printf("x: %d\n",x);

  int nextindex = playerindex + state->reversed;

  if(nextindex == x){
    nextindex = 0;
  }
  
  else if(nextindex == -1){
    nextindex = x-1;
  }
  
  while(state->players[nextindex].alive == 0){
      
      
    nextindex += state->reversed;
    if(nextindex == x){
      nextindex = 0;
    }
    else if(nextindex == -1){
      nextindex = x-1;
    }
    
  }

  //printf("nextindex: %d\n", nextindex);
  
  
  if(new){
    memset(state->testing,0,BUFFER_SIZE);
  }
  
  int i = 0;
  for(i; i<20; i++){
    if(state->players[playerindex].hand[i] == abs(input) && input != 1){
      state->players[playerindex].hand[i] = NONE;
      break;
    }
  }

  for(i; i<20;i++){
    if(state->players[playerindex].hand[i+1]){
      state->players[playerindex].hand[i] = state->players[playerindex].hand[i+1];
    }
  }

  if(input == 1){
    strcpy(output, "1 ");

    int j = 0;
    strcat(output, " Your hand: ");
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }
    strcat(output, "\n");
    strcat(output, "You dumdum, you can't play a defuse card!\n");

    write(client_socket,output,BUFFER_SIZE);
    read(client_socket, buffer, BUFFER_SIZE);    
    process_action(client_socket,state,buffer,playerindex,0);
  }

  //ENTER WILL REPRESENT DRAWING A CARD
  if(input == 0){
    if(state->players[playerindex].attacked){
      strcpy(output, "1 ");
      char tmp[100];
      //sprintf(tmp, "attacked: %d\n",state->players[playerindex].attacked);
      //strcat(buffer, tmp);
      //strcat(buffer, "draw ");
      strcat(output, "You were attacked, so you must take two turns!\n");
    }
    else{
      strcpy(output, "0 ");
      //strcat(buffer, "draw ");
      //char tmp[100];
      //sprintf(tmp, "attacked: %d\n",state->players[playerindex].attacked);
      //strcat(buffer, tmp);
    }
    
    strcat(output,draw(client_socket,state, playerindex));

    int j = 0;
    strcat(output, " Your hand: ");
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }
    strcat(output, "\n");

    //char tmp[256];
    //sprintf(tmp, "%s drew a card!\n", state->players[playerindex].name);
    //strcat(state->testing, tmp);
    
    write(client_socket, output, BUFFER_SIZE);

    if(state->players[playerindex].attacked){
      state->players[playerindex].attacked = 0;
      //printf("attacked: %d\n",state->players[playerindex].attacked);
      read(client_socket, buffer, BUFFER_SIZE);    
      process_action(client_socket,state,buffer,playerindex,0);
    }
    else{
      char timp[256];
      sprintf(timp, "%s's turn...", state->players[nextindex].name);
      strcat(state->testing,timp);
    }
    

  }

  else if(input == SKIP){
    if(state->players[playerindex].attacked){
      strcpy(output, "1 ");
      strcat(output, "You were attacked, so you must take two turns!\n");
    }
    else{
      strcpy(output, "0 ");
    }
    
    strcat(output, "You skipped and ended your turn!\n");
    
    strcat(output, "  ");
    strcat(output, " Your hand: ");
    int j=0;
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }

    strcat(output, "\n");

    char tmp[256];
    sprintf(tmp, "%s skipped their turn!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);
    write(client_socket, output, BUFFER_SIZE);

    if(state->players[playerindex].attacked){
      state->players[playerindex].attacked = 0;
      read(client_socket, buffer, BUFFER_SIZE);
      process_action(client_socket,state,buffer,playerindex,0);
    }
    else{
      char timp[256];
      sprintf(timp, "%s's turn...", state->players[nextindex].name);
      strcat(state->testing,timp);
    }
    

  }

  else if(input == SEE_THE_FUTURE){
    strcpy(output, "1 ");
    strcat(output, "~The Future~: ");
    
    strcat(output, thefuture(state));
    
    strcat(output, "  ");
    strcat(output, "\n");
    strcat(output, " Your hand: ");
    int j=0;
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }
    
    strcat(output, "\n");

    
    char tmp[256];
    sprintf(tmp, "%s saw the future!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);

    write(client_socket, output, BUFFER_SIZE);
    read(client_socket, buffer, BUFFER_SIZE);
    printf("Received [%s] from client",buffer);
    process_action(client_socket,state,buffer,playerindex,0);

  }

  else if(input == SHUFFLE){
    strcpy(output, "1 ");
    strcat(output, "The deck's been shuffled!\n");
    shuffle_deck(state);   
    
    strcat(output, "  ");
    strcat(output, " Your hand: ");
    int j=0;
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }
    strcat(output, "\n");

    char tmp[256];
    sprintf(tmp, "%s shuffled the deck!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);

    write(client_socket, output, BUFFER_SIZE);
    read(client_socket, buffer, BUFFER_SIZE);
    printf("Received [%s] from client",buffer);
    process_action(client_socket,state,buffer,playerindex,0);


  }

  else if(input == ATTACK){
    state->players[playerindex].attacked = 0;
    strcpy(output, "0 ");

    int x = 0;
    for(x;x<6;x++){
      if(state->players[x].name[0] == 0){
	break;
      }
    }

    int attackedindex = playerindex + state->reversed;
    if(attackedindex == x){
      attackedindex = 0;
    }
    else if(attackedindex == -1){
      attackedindex = x-1;
    }
    
    while(state->players[attackedindex].alive == 0){
     

      attackedindex += state->reversed;
      if(attackedindex == x){
	attackedindex = 0;
      }
      else if(attackedindex == -1){
	attackedindex = x-1;
      }
      
    }



    
    state->players[attackedindex].attacked = 1;

    char tmp[BUFFER_SIZE];
    sprintf(tmp, "You attacked %s, forcing them to take two turns!\n", state->players[attackedindex].name);
    strcat(output,tmp);
    
    strcat(output, "  ");
    int j=0;
    strcat(output, " Your hand: ");
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }

    strcat(output, "\n");

    char tzmp[256];
    sprintf(tzmp, "%s attacked %s!\n", state->players[playerindex].name, state->players[attackedindex].name);
    strcat(state->testing, tzmp);
    char timp[256];
    sprintf(timp, "%s's turn...", state->players[nextindex].name);
    strcat(state->testing,timp);
  
    write(client_socket, output, BUFFER_SIZE);

    
  }

  else if(input == REVERSE){

    if(state->players[playerindex].attacked){
      strcpy(output, "1 ");
      strcat(output, "You were attacked, so you must take two turns!\n");
    }
    else{
      strcpy(output, "0 ");
    }
    strcat(output, "You reversed the order of play!\n");

    state->reversed *= -1;
    
    strcat(output, "  ");
    strcat(output, " Your hand: ");
    int j=0;
    for (j;j<20;j++){
      int card = (state->players[playerindex]).hand[j];
      if(card == NONE){
	break;
      }
      char temp[256];
      sprintf(temp,"[%s] ", cardtotext(card));
      strcat(output, temp);
    }
    strcat(output, "\n");

    char tmp[256];
    sprintf(tmp, "%s reversed the order of play!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);

    write(client_socket, output, BUFFER_SIZE);
    if(state->players[playerindex].attacked){
      state->players[playerindex].attacked = 0;
      read(client_socket, buffer, BUFFER_SIZE);
      process_action(client_socket,state,buffer,playerindex,0);
    }
    
    else{
      nextindex = playerindex + state->reversed;

      if(nextindex == x){
	nextindex = 0;
      }
  
      else if(nextindex == -1){
	nextindex = x-1;
      }
  
      while(state->players[nextindex].alive == 0){
      
      
	nextindex += state->reversed;
	if(nextindex == x){
	  nextindex = 0;
	}
	else if(nextindex == -1){
	  nextindex = x-1;
	}
    
      }

      char timp[256];
      sprintf(timp, "%s's turn...", state->players[nextindex].name);
      strcat(state->testing,timp);
    }
    //printf("Received [%s] from client",buffer);


  }

  else if(input == FAVOR){
    strcpy(output, "3 ");
    strcat(output, "Choose a player to steal from:\n");
    int i = 0;
    for(i;i<6;i++){
      char tump[20];
      if(state->players[i].name[0] && i != playerindex && state->players[i].alive){
	sprintf(tump, "%d: %s\n",i,state->players[i].name);
	strcat(output, tump);	
      }
    }
    
    char tmp[50];
    memset(tmp,0,50);
    write(client_socket,output, BUFFER_SIZE);
    memset(output,0,BUFFER_SIZE);
    read(client_socket,tmp,50);
    strcat(output, "0 ");
    strcat(output, "Waiting for card...\n");
    int favor = atoi(tmp);

    state->favor = favor;
    state->benefactor = playerindex;


    char tomp[256];
    sprintf(tomp, "%s requested a card from %s!\n", state->players[playerindex].name, state->players[favor].name);
    strcat(state->testing, tomp);

  }

  else if(input < 0){

    //    printf("made it to less than 0 case\n");
    strcpy(output, "0 ");
    strcat(output, "We are sorry for your loss.\n");

    int transfer = input / -1;

    printf("transfer: %d\n", transfer);


    i=0;
    for(i;i<20;i++){
      int card = (state->players[state->benefactor]).hand[i];
      if(card == NONE){
	break;
      }
    }
    (state->players[state->benefactor]).hand[i] = transfer;
    (state->players[state->benefactor]).hand[i+1] = NONE;

    //printf("transferred cards\n");
    

    char tomp[256];
    sprintf(tomp, "%s has given a card to %s.\n", state->players[playerindex].name, state->players[state->benefactor].name);

    //printf("sprintf'd\n");

    state->favor = -1;

    strcat(state->testing, tomp);
    write(client_socket, output, BUFFER_SIZE);

  }

  else if(input > 200){
    strcpy(output, "3 ");
    strcat(output, "Choose a player to steal from:\n");
    int i = 0;
    for(i;i<6;i++){
      char tump[20];
      if(state->players[i].name[0] && i != playerindex && state->players[i].alive){
	sprintf(tump, "%d: %s\n",i,state->players[i].name);
	strcat(output, tump);	
      }
    }
    char tmp[50];
    memset(tmp,0,50);
    write(client_socket,output, BUFFER_SIZE);
    memset(output,0,BUFFER_SIZE);
    memset(tmp,0,50);
    read(client_socket,tmp,BUFFER_SIZE);

    if(input < 300){
      //DOUBLE CODE
      strcpy(output,"1 ");
      

      int victim = atoi(tmp);

      i = 0;
      int end = 20;
      for(i;i < 20; i++){
	if(state->players[victim].hand[i] == NONE){
	  end = i;
	  break;
	}
      }

      int randint;
      srand(time(NULL));
      randint = rand() % end;

      i = randint;
      int newcard =  state->players[victim].hand[randint];
      state->players[victim].hand[randint] = NONE;

      for(i; i<20;i++){
	if(state->players[victim].hand[i+1]){
	  state->players[victim].hand[i] = state->players[victim].hand[i+1];
	}
      }
      
      i=0;
      for(i;i<20;i++){
	int card = (state->players[playerindex]).hand[i];
	if(card == NONE){
	  break;
	}
      }
      (state->players[playerindex]).hand[i] = newcard;
      (state->players[playerindex]).hand[i+1] = NONE;

      int j = 0;
	
      for(j;j<2;j++){
	i = 0;
	for(i; i<20; i++){
	  if(state->players[playerindex].hand[i] == input - 200){
	    state->players[playerindex].hand[i] = NONE;
	    break;
	  }
	}

	for(i; i<20;i++){
	  if(state->players[playerindex].hand[i+1]){
	    state->players[playerindex].hand[i] = state->players[playerindex].hand[i+1];
	  }
	}
      }
      
      char tomp[256];
      sprintf(tomp, "%s played two cards and got a random card from %s!\n", state->players[playerindex].name, state->players[victim].name);
      strcat(state->testing, tomp);
      memset(tomp,0,256);
      
      sprintf(tomp, "You got a %s from %s!\n", cardtotext(newcard), state->players[victim].name);
      strcat(output,tomp);

      strcat(output, " Your hand: ");
      j=0;
      for (j;j<20;j++){
	int card = (state->players[playerindex]).hand[j];
	if(card == NONE){
	  break;
	}
	char temp[256];
	sprintf(temp,"[%s] ", cardtotext(card));
	strcat(output, temp);
      }
      strcat(output, "\n");

      

    }
    else{
      //TRIPLE CODE
      strcpy(output,"5 ");

      i = 0;
      int victim = 0;
      for(i;i<6;i++){
	if(strcmp(state->players[i].name,tmp) == 0){
	  victim = i;
	  break;
	}
      }
      char temp[256];
      sprintf(temp,"State the card you would like from %s:\n", state->players[victim].name);
      strcat(output, temp);
      i=1;
      for(i;i<13;i++){
	char tump[20];
	sprintf(tump, "%d: %s\n", i, cardtotext(i));
	strcat(output, tump);
      }
      write(client_socket, output, BUFFER_SIZE);
      memset(temp,0,256);
      memset(output,0,BUFFER_SIZE);
      read(client_socket, temp, BUFFER_SIZE);
      strcpy(output, "1 ");

      int chosen = atoi(temp);

      //printf("chosen: %d\n",chosen);

      i = 0;
      int found = 0;
      for(i;i<20;i++){
	if(state->players[victim].hand[i] == chosen){
	  found = 1;
	  break;
	}
      }

      //printf("chosen: %d\n",chosen);

      //printf("!!!!A!!!!: %d", i);
      if(found){

	state->players[victim].hand[i] = NONE;
	//printf("!!!!B!!!!: %d", i);
	for(i; i<20;i++){
	  if(state->players[victim].hand[i+1]){
	    //printf("!!!!C!!!!: %d", i);
	    state->players[victim].hand[i] = state->players[victim].hand[i+1];
	  }
	}

	//printf("chosen: %d\n",chosen);
	
	
	//printf("!!!!D!!!!: %d", i);
	i=0;
	for(i;i<20;i++){
	  int card = (state->players[playerindex]).hand[i];
	  if(card == NONE){
	    break;
	  }
	}
	(state->players[playerindex]).hand[i] = chosen;
	(state->players[playerindex]).hand[i+1] = NONE;

	//printf("chosen: %d\n",chosen);

	int j = 0;
	
	for(j;j<3;j++){
	  i = 0;
	  for(i; i<20; i++){
	    if(state->players[playerindex].hand[i] == input - 300){
	      state->players[playerindex].hand[i] = NONE;
	      break;
	    }
	  }
	  
	  for(i; i<20;i++){
	    if(state->players[playerindex].hand[i+1]){
	      state->players[playerindex].hand[i] = state->players[playerindex].hand[i+1];
	    }
	  }
	}

	//printf("chosen: %d\n",chosen);

	char tomp[256];
	sprintf(tomp, "%s played three cards and got a %s card from %s!\n", state->players[playerindex].name, cardtotext(chosen) ,state->players[victim].name);
	strcat(state->testing, tomp);
	memset(tomp,0,256);

	sprintf(tomp, "You got a %s from %s!\n", cardtotext(chosen), state->players[victim].name);
	strcat(output,tomp);

	strcat(output, " Your hand: ");
	j=0;
	for (j;j<20;j++){
	  int card = (state->players[playerindex]).hand[j];
	  if(card == NONE){
	    break;
	  }
	  char temp[256];
	  sprintf(temp,"[%s] ", cardtotext(card));
	  strcat(output, temp);
	}
	strcat(output, "\n");
	
	
      }
      else{
	strcat(output, "They didn't have that card! Better luck next time! ¯\\_(ツ)_/¯\n");
	char tomp[256];
	sprintf(tomp, "%s played three cards, asked for a %s card from %s, but they didn't have any!\n", state->players[playerindex].name, cardtotext(chosen) ,state->players[victim].name);
	strcat(state->testing, tomp);
	memset(tomp,0,256);
	int j = 0;
	for(j;j<3;j++){

	  i = 0;
	  for(i; i<20; i++){
	    if(state->players[playerindex].hand[i] == input - 300){
	      state->players[playerindex].hand[i] = NONE;
	      break;
	    }
	  }
	  
	  for(i; i<20;i++){
	    if(state->players[playerindex].hand[i+1]){
	      state->players[playerindex].hand[i] = state->players[playerindex].hand[i+1];
	    }
	  }
	}
      }
      
    }

    write(client_socket, output, BUFFER_SIZE);
    read(client_socket, buffer, BUFFER_SIZE);
    //printf("Received [%s] from client",buffer);
    process_action(client_socket,state,buffer,playerindex,0);
    

  }

  //printf("processing: %s\n",state->testing);
  


  //More inputs to be included here
}

char * thefuture(struct game_state * state){
  int i = 0;
  char * thefuture = malloc(100);

  int end = 57;
  for(i;i < 57; i++){
    if((state->deck)[i] == 13){
      end = i;
      break;
    }
  }

  for(i=end-1;i>end-4;i--){
    int card = (state->deck)[i];
    char temp[50];
    sprintf(temp," %s, ",cardtotext(card));
    strcat(thefuture, temp);
  }

  return thefuture;
}

char * draw(int client_socket, struct game_state *state, int playerindex){
  char * outputstring = malloc(BUFFER_SIZE);
  memset(outputstring,0,BUFFER_SIZE);
  strcat(outputstring, "2 ");
  //Removes card from end of deck
  int i = 0;
  int end = 57;
  for(i;i < 57; i++){
    if((state->deck)[i] == 13){
      end = i;
      break;
    }
  }
  int drawncard = (state->deck)[end-1];
  (state->deck)[end-1] = NONE;
  int defuse = 0;

  if(drawncard == EXPLODING_KITTEN){
    strcat(outputstring,"You drew an EXPLODING KITTEN!!!\n");
    int j = 0;
    for(j;j<20 && (state->players[playerindex].hand[j] != NONE);j++){
      if(state->players[playerindex].hand[j] == DEFUSE && !defuse){
	strcat(outputstring,"...but were saved by your DEFUSE card!\n");
	char tamp[5];
	sprintf(tamp,"You may reinsert the kitten anywhere from indices 0-%d (%d being the top card).\n",end-1,end-1);
	strcat(outputstring,tamp);
	defuse = 1;
	
      }
      
      if(defuse && state->players[playerindex].hand[j+1]){
	state->players[playerindex].hand[j] = state->players[playerindex].hand[j+1];
      }
    }

    if(!defuse){
      strcat(outputstring,"...and were blown to meowing smithereens!!!!\n");
      strcat(outputstring,"******YOU DIED!******\n");
      char tmp[256];
      sprintf(tmp, "%s drew an EXPLODING KITTEN and DIED!\n", state->players[playerindex].name);
      strcat(state->testing, tmp);
	
      state->players[playerindex].alive = 0;
    }
  }


  if(drawncard != EXPLODING_KITTEN){
    //Adds drawncard to player's hand
    i=0;
    for(i;i<20;i++){
      int card = (state->players[playerindex]).hand[i];
      if(card == NONE){
	break;
      }
    }
    (state->players[playerindex]).hand[i] = drawncard;
    (state->players[playerindex]).hand[i+1] = NONE;
  }
  
  //Returns a string of what card the player drew
  char temp[BUFFER_SIZE];
  memset(temp,0,BUFFER_SIZE);
  if(drawncard != EXPLODING_KITTEN){
    sprintf(temp," You drew a %s!\n",cardtotext(drawncard));
  }

  if(defuse){
    char tmp[5];
    memset(tmp,0,5);
    write(client_socket, outputstring, BUFFER_SIZE);
    read(client_socket, tmp, 5);
    int kittyindex = atoi(tmp);
    int f = end;
    for(f;f>kittyindex;f--){
      int saved = state->deck[f-1];
      state->deck[f-1] = state->deck[f];
      state->deck[f]=saved;
    }
    state->deck[kittyindex] = EXPLODING_KITTEN;
  }

  if(drawncard != EXPLODING_KITTEN){
    strcat(outputstring, temp);
    char tmp[256];
    sprintf(tmp, "%s drew a card!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);
  }

  else if(!defuse){
    strcat(outputstring, temp);
  }
  
  else{
    strcpy(outputstring, "The kitten has been re-inserted...\n");
    char tmp[256];
    sprintf(tmp, "%s drew an EXPLODING KITTEN but had a DEFUSE card!\n", state->players[playerindex].name);
    strcat(state->testing, tmp);
    memset(tmp,0,256);
    sprintf(tmp, "%s deliberately put the kitten somewhere in the deck...\n", state->players[playerindex].name);
    strcat(state->testing, tmp);
  }
  return outputstring;
  
  
}

char * cardtotext(int cardid){
  char * result = malloc(30);
  memset(result,0,30);

  if(cardid == 0){
    strcpy(result,"EXPLODING KITTEN");
  }
  else if(cardid == 1){
    strcpy(result,"DEFUSE");
  }
  else if(cardid == 2){
    strcpy(result,"ATTACK");
  }
  else if(cardid == 3){
    strcpy(result,"SKIP");
  }
  else if(cardid == 4){
    strcpy(result,"FAVOR");
  }
  else if(cardid == 5){
    strcpy(result,"SHUFFLE");
  }
  else if(cardid == 6){
    strcpy(result,"SEE THE FUTURE");
  }
  else if(cardid == 7){
    strcpy(result,"REVERSE");
  }
  else if(cardid == 8){
    strcpy(result,"TACOCAT");
  }
  else if(cardid == 9){
    strcpy(result,"CATTERMELON");
  }
  else if(cardid == 10){
    strcpy(result,"POTATO CAT");
  }
  else if(cardid == 11){
    strcpy(result,"BEARD CAT");
  }
  else if(cardid == 12){
    strcpy(result,"RAINBOW RALPHING CAT");
  }

  return result;
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
      (state->deck)[i] = REVERSE;
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
      //printf("[%s]'s hand: \n", (state->players[i]).name);
      j = 0;
      for(j;j<20;j++){
	//printf("[%d] ", (state->players[i]).hand[j]);
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

