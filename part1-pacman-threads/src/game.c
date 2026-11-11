#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>


#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4



void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    command_t c; 
    if (pacman->n_moves == 0) { // if is user input
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;
        
        if (c.command == 'G') {
            return CREATE_BACKUP; // fazer o fork e criar o backup
        }

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }

    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

//função para o qsort mais abaixo
int compare_levels(const void *a, const void *b) { 
    return strcmp((const char *)a, (const char *)b);
}

void* ghost_thread_fn(void* args) {
    thread_params_t* params = (thread_params_t*)args;
    board_t* board = params->actual_board;
    int ghost_id = params->id;
    ghost_t* ghost = &board->ghosts[ghost_id];

    while (board->game_started == 0) { 
        if (board->game_exit_code != 0){
            return NULL; 
        } 
        sleep_ms(10);
    }

    while (board->game_exit_code == 0 && board->pacmans[0].alive) {
        
        // forma de calcular o ghost->moves que é um array circular
        command_t* cmd = &ghost->moves[ghost->current_move % ghost->n_moves];

        pthread_mutex_lock(&board->board_lock);
        
        // verificação se o jogo não acabou enquanto esperava pelo lock
        if (board->game_exit_code == 0 && board->pacmans[0].alive) {
            move_ghost(board, ghost_id, cmd);
        }
        
        pthread_mutex_unlock(&board->board_lock);

        sleep_ms(board->tempo);
    }

    return NULL;
}

void* pacman_thread_fn(void* args){
    thread_params_t* params = (thread_params_t*)args;
    board_t* board = params->actual_board;
    pacman_t* pacman = &board->pacmans[0];

    

    while (board->game_exit_code == 0){
        command_t cmd_struct;
        command_t* cmd = NULL;
        
        pthread_mutex_lock(&board->board_lock);

        if (pacman->n_moves > 0) { //modo automatico
            cmd = &pacman->moves[pacman->current_move % pacman->n_moves];
        } else { //modo manual
            if (board->pacman_next_move != 0) {
                cmd_struct.command = board->pacman_next_move;
                cmd_struct.turns = 1;
                cmd = &cmd_struct;
                board->pacman_next_move = 0; 
            }
        }


        if (cmd != NULL){
            int move_result = move_pacman(board, 0, cmd);

            if (move_result == REACHED_PORTAL) {
                board->game_exit_code = NEXT_LEVEL; 
            } 
            if (move_result == GAME_EXIT) {
                board->game_exit_code = QUIT_GAME; 
            }
            if (move_result == GAME_SAVE) {
                board->game_exit_code = CREATE_BACKUP; 
            }
            else if (move_result == DEAD_PACMAN) {
                board->game_exit_code = QUIT_GAME; 
            }
        }

        pthread_mutex_unlock(&board->board_lock);

        if (board->game_exit_code != 0){ //se o jogo acabou sair do loop
            break;
        } 

        sleep_ms(board->tempo);

    }
    return NULL;
}

int main(int argc, char** argv) {
    DIR *dir;
    char *level_dir;
    struct dirent *readfiles;
    char level_files[MAX_LEVELS][MAX_FILENAME];
    int level_count = 0;
    int has_backup = 0;

    //id das threads para o join
    pthread_t ghost_threads[MAX_GHOSTS];
    pthread_t pacman_thread;

    //argumentos das threads
    thread_params_t ghost_params[MAX_GHOSTS]; 
    thread_params_t pacman_params;

    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    level_dir = argv[1];

    dir = opendir(level_dir);
    if(dir == NULL){
        fprintf(stderr, "The directory %s does NOT exist!\n", level_dir);
        return 1;
    }

    while ((readfiles = readdir(dir)) != NULL){
        if (strcmp(readfiles->d_name, ".") == 0 || strcmp(readfiles->d_name, "..") == 0) {
            continue;
        }

        if (strstr(readfiles->d_name, ".lvl")) {

            if (level_count<MAX_LEVELS){
                snprintf(level_files[level_count], MAX_FILENAME, "%s/%s", level_dir, readfiles->d_name);
                level_count++;
            }  
        }
    }

    closedir(dir); 
    //organiza o array de levels de modo a que o .1 seja o primeiro, o .2 o segundo, etc.
    qsort(level_files, level_count, MAX_FILENAME, compare_levels);

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;


    for(int i = 0; i < level_count && !end_game; i++) {

        load_level(&game_board, level_files[i], level_dir, accumulated_points);

        if (pthread_mutex_init(&game_board.board_lock, NULL) != 0) {
            perror("erro a inicializar o mutex");
            return 1;
        }

        game_board.game_exit_code = 0;
        game_board.game_started = 0;

        pacman_params.id = 0;
        pacman_params.actual_board = &game_board;

        pthread_create(&pacman_thread, NULL, pacman_thread_fn, &pacman_params);

        for (int k = 0; k < game_board.n_ghosts; k++) { 
            ghost_params[k].id = k;
            ghost_params[k].actual_board = &game_board;

            pthread_create(&ghost_threads[k], NULL, ghost_thread_fn, &ghost_params[k]);
        }

        pthread_mutex_lock(&game_board.board_lock);
        draw_board(&game_board, DRAW_MENU);
        pthread_mutex_unlock(&game_board.board_lock);
        refresh_screen();

        
        if (game_board.pacmans[0].n_moves == 0) { 
            char start_input = '\0';
            
            while (start_input != 'W' && start_input != 'A' && start_input != 'S' && start_input != 'D') {
                start_input = get_input();
                
                if (start_input == 'Q') {
                    end_game = true;
                    pthread_mutex_lock(&game_board.board_lock);
                    game_board.game_exit_code = QUIT_GAME;
                    game_board.game_started = 1; 
                    pthread_mutex_unlock(&game_board.board_lock);
                    break;
                }
                
                sleep_ms(10);
            }

            if (!end_game) {
                pthread_mutex_lock(&game_board.board_lock);
                game_board.pacman_next_move = start_input;
                pthread_mutex_unlock(&game_board.board_lock);
            }
        } 

        if (!end_game) {
            pthread_mutex_lock(&game_board.board_lock);
            game_board.game_started = 1; 
            pthread_mutex_unlock(&game_board.board_lock);
        }
        
        while(true) {

            pthread_mutex_lock(&game_board.board_lock);
            int game_ending = game_board.game_exit_code;
            pthread_mutex_unlock(&game_board.board_lock);

            //jogo acabou ou passou o nivel
            if (game_ending == NEXT_LEVEL) {
                if (i == level_count - 1) {
                    end_game = true;
                }
                break;
            } else if (game_ending == QUIT_GAME) {
                if (has_backup && !game_board.pacmans[0].alive){
                    exit(LOAD_BACKUP);
                }
                end_game = true;
                break;
            }

            char input;

            //se o 'G' está no ficheiro simulamos o input, se nao esperamos pelo player
            if (game_ending == CREATE_BACKUP) {
                input = 'G'; 
            } else {
                input = get_input(); 
            }

            if (input == 'Q') {
                pthread_mutex_lock(&game_board.board_lock);
                game_board.game_exit_code = QUIT_GAME;
                pthread_mutex_unlock(&game_board.board_lock);

                if (has_backup && !game_board.pacmans[0].alive){
                    exit(LOAD_BACKUP);
                } 
                if (has_backup){
                    exit(0);
                }
                end_game = true;
                break;
            }   

            if (input == 'G') {
                
                
                if (game_ending == CREATE_BACKUP) {
                     pthread_join(pacman_thread, NULL);
                     for (int k = 0; k < game_board.n_ghosts; k++) {
                         pthread_join(ghost_threads[k], NULL);
                     }
                }

                if (has_backup == 0){ //ja ha algum backup?
                    
                    pthread_mutex_lock(&game_board.board_lock);
                    
                    pid_t pid = fork();

                    if (pid < 0) {
                        perror("Erro no fork");
                        pthread_mutex_unlock(&game_board.board_lock);
                    }

                    if (pid == 0){ //filho
                        pthread_mutex_unlock(&game_board.board_lock);
                        has_backup = 1;

                        //forma de reiniciar as threads no filho pois o filho só herda a thread
                        //onde o fork foi chamado, ou seja, a main
                        pacman_params.actual_board = &game_board; 
                        pthread_create(&pacman_thread, NULL, pacman_thread_fn, &pacman_params);
                        
                        for (int k = 0; k < game_board.n_ghosts; k++) {
                            ghost_params[k].actual_board = &game_board;
                            pthread_create(&ghost_threads[k], NULL, ghost_thread_fn, &ghost_params[k]);
                        }
                        
                        refresh_screen();
                    }
                    else { //pai
                        int status;
                        if (wait(&status) == -1) { 
                            perror("Erro no wait");
                        }
                        pthread_mutex_unlock(&game_board.board_lock);

                        //limpamos o código de exit para continuar o jogo
                        pthread_mutex_lock(&game_board.board_lock);
                        game_board.game_exit_code = 0; 
                        pthread_mutex_unlock(&game_board.board_lock);
                        
                        
                        //se backup é feito atraves do .p as threads morrem
                        if (game_ending == CREATE_BACKUP) {
                             pthread_create(&pacman_thread, NULL, pacman_thread_fn, &pacman_params);
                             for (int k = 0; k < game_board.n_ghosts; k++) {
                                 pthread_create(&ghost_threads[k], NULL, ghost_thread_fn, &ghost_params[k]);
                             }
                        }

                        if (WIFEXITED(status)) {
                            int exit_code = WEXITSTATUS(status);

                            if (exit_code == LOAD_BACKUP){
                                refresh_screen();
                                continue;
                            } else {
                                end_game = true;
                                break;
                            }
                        }
                    }
                } else {
                    //se ja existe um backup o seguintes sao ignorados
                    if (game_ending == CREATE_BACKUP) {
                         pthread_mutex_lock(&game_board.board_lock);
                         game_board.game_exit_code = 0;
                         pthread_mutex_unlock(&game_board.board_lock);

                         pthread_create(&pacman_thread, NULL, pacman_thread_fn, &pacman_params);
                         for (int k = 0; k < game_board.n_ghosts; k++) {
                             pthread_create(&ghost_threads[k], NULL, ghost_thread_fn, &ghost_params[k]);
                         }
                    }
                }
            }

            else if (game_board.pacmans[0].n_moves == 0){
                if (input == 'W' || input == 'A' || input == 'S' || input == 'D') {
                    pthread_mutex_lock(&game_board.board_lock);
                    game_board.pacman_next_move = input;
                    pthread_mutex_unlock(&game_board.board_lock); 
                }
            }
    
            pthread_mutex_lock(&game_board.board_lock);
            draw_board(&game_board, DRAW_MENU);
            pthread_mutex_unlock(&game_board.board_lock);

            refresh_screen();

            sleep_ms(game_board.tempo);
                 
        }

        pthread_mutex_lock(&game_board.board_lock);
        //passar info as threads de que o jogo acabou
        if (game_board.game_exit_code == 0) { 
            game_board.game_exit_code = QUIT_GAME; 
        }
        pthread_mutex_unlock(&game_board.board_lock);

        //aqui fazemos o join das threads se ainda estiverem vivas 
        pthread_join(pacman_thread, NULL);

        for (int k = 0; k < game_board.n_ghosts; k++) {
            pthread_join(ghost_threads[k], NULL);
        }

        pthread_mutex_destroy(&game_board.board_lock);

        accumulated_points = game_board.pacmans[0].points;
        print_board(&game_board);
        unload_level(&game_board);

    }    

    terminal_cleanup();
    close_debug_file();

    return 0;
}
