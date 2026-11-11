#include "display.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#include "protocol.h"
#include "client_session.h"
#include "include/board.h"
#include "io.h"
#include "client_session.h"

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

PlayerSession *game_sessions; //global para ser vista no signal handler

int level_count = 0;

typedef struct {
    board_t *board;
    int ghost_index;
} ghost_thread_arg_t;

typedef struct {
    char req_pipe_path[40];
    char notif_pipe_path[40];
} connection_t;


//struct para buffer partilhado (Produtor-Consumidor)
typedef struct {
    connection_t *data;
    int size;
    int in;
    int out;
    int count;
    pthread_mutex_t lock;
    sem_t sem_full;  //pedidos prontos a processar
    sem_t sem_empty; //vagas livres no servidor
} client_buffer_t;

client_buffer_t client_queue = {
    .out = 0,
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};


typedef struct {
    int client_id;
    int score;
} player_score_t;

int server_running = 1;
char *server_pipe_path;
char *global_levels_dir;

sigset_t signal_mask;  //sinais para bloquear
int received_sigusr1 = 0;


int count_levels(const char *path) {
    int count = 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".lvl") == 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

int compare_scores(const void *a, const void *b) {
    player_score_t *p1 = (player_score_t *)a;
    player_score_t *p2 = (player_score_t *)b;
    return p2->score - p1->score; 
}

//função que indica 5 melhores clientes da sessão e printa num txt
void top5_scores(){
    int max_games = client_queue.size;

    player_score_t *temp_list = malloc(sizeof(player_score_t) * max_games);

    if (!temp_list){
        return;
    } 

    int count = 0; 

    for (int i = 0; i < max_games; i++){
        if (game_sessions[i].active){
            temp_list[count].client_id = game_sessions[i].client_id;
            temp_list[count].score = game_sessions[i].score;
            count++; 
        }
    }
    qsort(temp_list, count, sizeof(player_score_t), compare_scores);

    FILE *f = fopen("top5_scores.txt", "w");

    if (f){

        int limit;

        if (count <= 5) {
            limit = count;
        } else {
            limit = 5;
        }

        for (int i = 0; i < limit; i++) {
            fprintf(f, "Player %d: %d points\n", temp_list[i].client_id, temp_list[i].score);
        }
        fclose(f);
    } else {
        perror("[ERR] Erro ao abrir ficheiro de top5_scores");
    }
    free(temp_list);
}


void signal_handler(int sig) {
    /*
    if (signal(sig, signal_handler)) {
        exit(EXIT_FAILURE);
    }
    */
    received_sigusr1 = 1;
}

void init_server_structures(int max_games) {
    client_queue.size = max_games;
    client_queue.data = malloc(sizeof(connection_t) * max_games);
    client_queue.in = 0;
    client_queue.out = 0;
    client_queue.count = 0;

    pthread_mutex_init(&client_queue.lock, NULL);
    sem_init(&client_queue.sem_full, 0, 0);
    sem_init(&client_queue.sem_empty, 0, max_games);
}

void cleanup_server() {
    server_running = 0;
    unlink(server_pipe_path); 
    free(client_queue.data);
    pthread_mutex_destroy(&client_queue.lock);
    sem_destroy(&client_queue.sem_full);
    sem_destroy(&client_queue.sem_empty);
}

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();     
}

//thread que le o ficheiro moves vindo do client
void* input_thread(void *arg){
    board_t *board = (board_t*) arg;
    while (true) {
        char op_code;

        if (read(board->req_fd, &op_code, sizeof(char)) <= 0) {
            break;
        }

        if (op_code == OP_CODE_PLAY){
            char command;

            if (read(board-> req_fd, &command, sizeof(char)) > 0) {
                board->next_move = command;
            } 
        } else if (op_code == OP_CODE_DISCONNECT){
            break;
        }
    }
    return NULL;
}

//função que vai enviar o board atualizado ao cliente
void* sender_update_thread(void *arg) { 
    board_t *board = (board_t*) arg; 

    int last_score = -1;

    int board_size = 1 + (6 * sizeof(int)) + (board->width * board->height);
    char* board_data = malloc(board_size);
    int data_size = board->width * board->height;

    while (true){
        sleep_ms(board->tempo);

        pthread_rwlock_rdlock(&board->state_lock);
        if (board->thread_shutdown) {
            pthread_rwlock_unlock(&board->state_lock);
            pthread_exit(NULL);
        }

        int current_points = board->pacmans[0].points;

        int offset = 0;
        board_data[offset++] = OP_CODE_BOARD;


        int msg_params[6];

        msg_params[0] = board->width;
        msg_params[1] = board->height;
        msg_params[2] = board->tempo;
        msg_params[3] = board->victory; 
        msg_params[4] = board->game_over; 
        msg_params[5] = board->pacmans[0].points;

        memcpy(board_data + offset, &msg_params, sizeof(msg_params));
        offset += sizeof(msg_params);

        for (int i = 0; i < data_size; i++) {
            char c = board->board[i].content;
            if (c == ' ') {
                if (board->board[i].has_dot) {
                    c = '.';
                } else if (board->board[i].has_portal) {
                    c = '@';
                }
            } else if (board->board[i].content == 'W') {
                c = '#';
            }
            board_data[offset++] = c;
        }

        pthread_rwlock_unlock(&board->state_lock);

        
        if (current_points != last_score) {
            update_score(&game_sessions[board->worker_id], current_points);
            last_score = current_points;
        }

        if (write_all(board->notif_fd, board_data, board_size) == -1) {
            perror("Erro ao enviar atualização do tabuleiro");
            /*
            if (errno == EPIPE) {
                close(board->notif_fd);
            }
            */
            break;
        }
    }

    free(board_data);
    return NULL;
}



void* pacman_thread(void *arg) {
    board_t *board = (board_t*) arg;

    pacman_t* pacman = &board->pacmans[0];

    int *retval = malloc(sizeof(int));
    *retval = QUIT_GAME; 

    while (true) {
        

        sleep_ms(board->tempo * (1 + pacman->passo));

        command_t* play;
        command_t c;
        if (pacman->n_moves == 0) {
            c.command = board-> next_move;
            board->next_move = '\0';

            if(c.command == '\0') {
                continue;
            }

            c.turns = 1;
            play = &c;
        }
        else {
            play = &pacman->moves[pacman->current_move%pacman->n_moves];
        }
        
        if (play->command == 'Q') {
            *retval = QUIT_GAME;
            board->game_over = 1;
            break;
        }

        pthread_rwlock_wrlock(&board->state_lock);

        int result = move_pacman(board, 0, play);

        if (result == REACHED_PORTAL) {
            if (level_count - 1) {
                    board->victory = 1;
                }
            // Next level
            *retval = NEXT_LEVEL;
            pthread_rwlock_unlock(&board->state_lock);
            break;
        }
           

        if(result == DEAD_PACMAN) {
            // Restart from child, wait for child, then quit
            *retval = QUIT_GAME;
            board->game_over = 1;
            pthread_rwlock_unlock(&board->state_lock);
            break;
        }

        pthread_rwlock_unlock(&board->state_lock);
    }
    return (void*) retval;
}


void* ghost_thread(void *arg) {
    ghost_thread_arg_t *ghost_arg = (ghost_thread_arg_t*) arg;
    board_t *board = ghost_arg->board;
    int ghost_ind = ghost_arg->ghost_index;

    free(ghost_arg);

    ghost_t* ghost = &board->ghosts[ghost_ind];

    while (true) {
        sleep_ms(board->tempo * (1 + ghost->passo));

        pthread_rwlock_rdlock(&board->state_lock);
        if (board->thread_shutdown) {
            pthread_rwlock_unlock(&board->state_lock);
            pthread_exit(NULL);
        }
        
        move_ghost(board, ghost_ind, &ghost->moves[ghost->current_move%ghost->n_moves]);
        pthread_rwlock_unlock(&board->state_lock);
    }
}

//função para o qsort mais abaixo
int compare_levels(const void *a, const void *b) { 
    return strcmp((const char *)a, (const char *)b);
}



void play_game_session(int worker_id, int req_fd, int notif_fd) {

    srand(time(NULL) + getpid());

    int total_levels = count_levels(global_levels_dir);
    
    if (total_levels == 0) {
        printf("[Worker %d] Nenhum nível encontrado.\n", worker_id);
        return;
    }

    char level_files[total_levels][MAX_FILENAME];
    int loaded_count = 0;

    DIR* level_dir = opendir(global_levels_dir);
    if (level_dir == NULL) {
        printf("[Worker %d] Erro ao abrir diretoria de níveis\n", worker_id);
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(level_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char *dot = strrchr(entry->d_name, '.');
        if (dot && strcmp(dot, ".lvl") == 0) {

            if (loaded_count < total_levels) {
                strncpy(level_files[loaded_count], entry->d_name, MAX_FILENAME - 1);
                level_files[loaded_count][MAX_FILENAME - 1] = '\0'; 
                loaded_count++;
            }
        }
    }
    closedir(level_dir);

    //organiza o array de levels de modo a que o .1 seja o primeiro, o .2 o segundo, etc.
    qsort(level_files, loaded_count, MAX_FILENAME, compare_levels);

    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board = {0};

    //iteração sobre níveis já carregados e ordenados no array
    for (int i = 0; i < loaded_count; i++) {
        if (end_game) break;

        if (load_level(&game_board, level_files[i], global_levels_dir, accumulated_points) != 0) {
            fprintf(stderr, "[Worker %d] Erro: Falha ao carregar nível '%s'. Ignorando.\n", worker_id, level_files[i]);
            memset(&game_board, 0, sizeof(board_t));
            continue;
        }

        game_board.notif_fd = notif_fd;
        game_board.req_fd = req_fd;
        game_board.next_move = '\0';
        game_board.worker_id = worker_id;
        game_board.victory = 0;
        game_board.game_over = 0;

        while(true) {
            pthread_t sender_tid, pacman_tid, input_tid;
            pthread_t *ghost_tids = malloc(game_board.n_ghosts * sizeof(pthread_t));
            game_board.thread_shutdown = 0;

            pthread_create(&pacman_tid, NULL, pacman_thread, (void*) &game_board);
            for (int j = 0; j < game_board.n_ghosts; j++) {
                ghost_thread_arg_t *arg = malloc(sizeof(ghost_thread_arg_t));
                arg->board = &game_board;
                arg->ghost_index = j;
                pthread_create(&ghost_tids[j], NULL, ghost_thread, (void*) arg);
            }
            pthread_create(&sender_tid, NULL, sender_update_thread, (void*) &game_board);
            pthread_create(&input_tid, NULL, input_thread, (void*) &game_board);

            int *retval;
            pthread_join(pacman_tid, (void**)&retval);

            int result = *retval;
            free(retval);

            //verificar vitória antes de fazer thread_shutdown
            if(result == NEXT_LEVEL) {
                //é o ultimo nivel da lista?
                if (i == loaded_count - 1) {
                    pthread_rwlock_wrlock(&game_board.state_lock);
                    game_board.victory = 1;
                    pthread_rwlock_unlock(&game_board.state_lock);
                    
                    sleep_ms(300);  
                }
            }

            pthread_rwlock_wrlock(&game_board.state_lock);
            game_board.thread_shutdown = 1;
            pthread_rwlock_unlock(&game_board.state_lock);

            pthread_join(sender_tid, NULL);
            for (int j = 0; j < game_board.n_ghosts; j++) {
                pthread_join(ghost_tids[j], NULL);
            }

            pthread_cancel(input_tid); //esta pode estar bloqueada no read
            pthread_join(input_tid, NULL);
            free(ghost_tids);

            if(result == NEXT_LEVEL) {
                accumulated_points = game_board.pacmans[0].points;
                update_score(&game_sessions[worker_id], accumulated_points);
                break; 
            } 
            else if (result == QUIT_GAME) {
                end_game = true;
                accumulated_points = game_board.pacmans[0].points;  
                update_score(&game_sessions[worker_id], accumulated_points);    
                break; 
            }
        }
        
        unload_level(&game_board);
        memset(&game_board, 0, sizeof(board_t));
    }
}


void* worker_thread_fn(void* arg) {

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
        perror("[Worker] Erro ao bloquear SIGUSR1");
    }

    int id =*(int*) arg;
    free(arg);
    printf("[Worker %d] Pronto e à espera de clientes.\n", id);



    while (server_running) {
        //tentamos retirar um pedido do buffer
        sem_wait(&client_queue.sem_full); //bloqueia se nao houver pedidos

        pthread_mutex_lock(&client_queue.lock);
        if (!server_running) {
            pthread_mutex_unlock(&client_queue.lock);
            break;
        }


        connection_t req = client_queue.data[client_queue.out];
        client_queue.out = (client_queue.out + 1) % client_queue.size;
        client_queue.count--;

        pthread_mutex_unlock(&client_queue.lock);
        sem_post(&client_queue.sem_empty); //indica que ha uma vaga livre no buffer

        printf("[Worker %d] A atender cliente: %s\n", id, req.req_pipe_path);

        
        int notif_fd = open(req.notif_pipe_path, O_WRONLY);
        if (notif_fd == -1) {
            perror("Erro ao abrir o pipe de notificações do cliente");
            continue;
        }
        char resposta[2];
        resposta[0] = OP_CODE_CONNECT;
        resposta[1] = 0; //sucesso

        if(write(notif_fd, resposta, sizeof(resposta)) == -1){
            perror("Erro ao enviar ACK ao cliente");
            close(notif_fd);
            continue;
        };
        
        int req_fd = open(req.req_pipe_path, O_RDONLY);
        if (req_fd == -1) {
            perror("Erro ao abrir o pipe de pedidos do cliente");
            close(notif_fd);
            continue;
        }

    
        activate_session(&game_sessions[id], id, notif_fd, req_fd, req.req_pipe_path, req.notif_pipe_path);

        play_game_session(id, req_fd, notif_fd); //req?? notif?? ambos???
 
        close_session(&game_sessions[id]);
    }
    return NULL;
}

int main(int argc, char** argv) {

    struct sigaction sa_usr;
    sa_usr.sa_handler = signal_handler;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; //0 para que o open seja interrompido e possamos tratar o sinal

    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        perror("[Main] Erro ao configurar SIGUSR1");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, signal_handler) == SIG_ERR) {
    exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = SIG_IGN; //ignora SIGPIPE
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("[ERR]: Failed to ignore SIGPIPE");
        exit(EXIT_FAILURE);
    }

    int i; 
    //signal(SIGPIPE, SIG_IGN);

    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGUSR1); 
    

    if (argc != 4) {
        printf("Usage: %s <levels_dir> <max_games> <register_pipe>\n", argv[0]);
        return -1;
    }
    open_debug_file("server_log.txt");

    global_levels_dir = argv[1];
    int max_games= atoi(argv[2]); 
    server_pipe_path = argv[3];


    init_server_structures(max_games);
    
    game_sessions = malloc(sizeof(PlayerSession) * max_games);
    if (game_sessions == NULL) {
        perror("[Main] Erro fatal: falha no malloc de game_sessions");
        return 1;
    }
    
    init_session(game_sessions, max_games);

    if (mkfifo(server_pipe_path, 0640) == -1 && errno != EEXIST) {
        perror("[Main] Erro ao criar pipe do servidor");
        return 1;
    }

    //inicializacao de threads para n workers
    pthread_t workers[max_games];
    for(i = 0; i < max_games; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        if (pthread_create(&workers[i], NULL, worker_thread_fn, id) != 0) {
            perror("[Main] Erro ao criar worker");
            free(id);
        }
    }
    printf("[Main] Servidor iniciado em '%s'. Max Jogos: %d\n", server_pipe_path, max_games);

    while (server_running) {

        if (received_sigusr1){
            top5_scores();
            received_sigusr1 = 0;
        }
        
        int server_fd = open(server_pipe_path, O_RDONLY);
        if (server_fd == -1) {
            if (errno == EINTR) {
                //se jogo interrompido pelo sinal entao volta ao início do loop
                continue; 
            }
            perror("[Main] Erro ao abrir pipe servidor");
            break;
        }
        
        char op_code;

        while (read(server_fd, &op_code, 1) > 0) {
            
            if (op_code == OP_CODE_CONNECT) {
                connection_t new_req;
                
                if (read(server_fd, &new_req, sizeof(connection_t)) != sizeof(connection_t)) {
                    fprintf(stderr, "[Main] Erro: Mensagem de conexão incompleta\n");
                    continue;
                }

                sem_wait(&client_queue.sem_empty);
                pthread_mutex_lock(&client_queue.lock);

                //tratamento do buffer
                client_queue.data[client_queue.in] = new_req;
                client_queue.in = (client_queue.in + 1) % client_queue.size;
                client_queue.count++;

                pthread_mutex_unlock(&client_queue.lock);

                //avisa worker que ha trabalho
                sem_post(&client_queue.sem_full);
            }
            else {
                 fprintf(stderr, "[Main] OpCode desconhecido recebido no pipe de registo: %d\n", op_code);
            }
        }
        
        close(server_fd);
    }

    cleanup_server();
    free(game_sessions);
    return 0;
}