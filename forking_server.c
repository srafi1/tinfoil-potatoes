#include "networking.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <signal.h>
#define SEM_KEY 1492
#define SHM_KEY 1776
#define MAX_PLAYERS 6

void process(char *s);
void subserver(int from_client, int index);
void setup_shm();
void sighandler(int signum);
void post_setup(int num_players);

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

struct game_state {
    char players[6][15];
    int current_player;
    int deck[30]; //TODO: change 30 to number of possible cards in a deck
    int cards_left;
    int turn_completed;
    int received_update[6];
    char testing[BUFFER_SIZE];
};

int subserver_pids[MAX_PLAYERS];

int main() {
    setup_shm();
    signal(SIGINT, sighandler);

    int listen_socket;
    int f;
    listen_socket = server_setup();

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
    int i;
    for (i = 0; i < 6; i++) {
        mem_loc->players[i][0] = 0;
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

    struct sembuf sb;
    sb.sem_op = -1;
    sb.sem_flg = SEM_UNDO;
    sb.sem_num = 0;
    semop(sem_desc, &sb, 1);

    read(client_socket, buffer, 15);
    printf("[subserver %d] player name: %s\n", getpid(), buffer);
    //copy char by char because assigning a pointer directly won't work with shm
    strncpy(mem_loc->players[index], buffer, 15);

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
                if (all_received_update) {
                    strcpy(buffer, "1 ");
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
        printf("%s\n", mem_loc->players[i]);
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
            printf("Player #%d turn: %s\n", mem_loc->current_player, mem_loc->players[mem_loc->current_player]);
        } else {
            //printf("waiting on someone to get update\n");
        }

        sb.sem_op = 1;
        semop(sem_desc, &sb, 1);
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
