#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>

FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        case 'Q':
            return GAME_EXIT;
        case 'G':
            pac->current_move += 1; // move on
            return GAME_SAVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

int ler_linha(int fd, char *buffer, int max_len) {
    int bytes_lidos = 0;
    char char_atual;
    int n_read = 0;

    while (bytes_lidos < max_len - 1) {
        int result = read(fd, &char_atual, 1);

        if (result <= 0) { 
            break; 
        }

        n_read = 1; // Lemos pelo menos um byte com sucesso

        if (char_atual == '\n') { 
            break;
        }

        buffer[bytes_lidos] = char_atual;
        bytes_lidos++;
    }

    buffer[bytes_lidos] = '\0';

    if (bytes_lidos == 0 && n_read > 0) {
        return 1;
    }
    
    return bytes_lidos;
}

int load_pacman(board_t* board, const char* pacman_path, int points) {

    //obter a referencia para o pacman
    pacman_t* pacman = &board->pacmans[0];
    
    if (pacman_path == NULL){ //modo manual caso o ficheiro .lvl nao contenha um .p
        
        pacman->n_moves = 0; 
        pacman->passo = 0;
        pacman->pos_y = 1; // Posição default, ajustar ou nao
        pacman->pos_x = 1; // Posição default, ajustar ou nao
        pacman->alive = 1;
        pacman->current_move = 0;
        pacman->waiting = 0;
        pacman->points = points;

        board->board[pacman->pos_y * board->width + pacman->pos_x].content = 'P';
        return 0; 
    }

    pacman -> n_moves = 0;
    pacman->alive = 1;
    pacman->current_move = 0;
    pacman->waiting = 0;


    char linha_lida[1024];
    int defenicoes_lidas = 0; //flag para saber quando comecar a ler os movimentos
    int pacman_fd = open(pacman_path, O_RDONLY);

    if (pacman_fd < 0) {
        printf("Erro a abrir ficheiro .p\n");
        return 1;
    }

    while (ler_linha(pacman_fd, linha_lida, sizeof(linha_lida)) > 0) {
        //ignoramos lihas com comentarios e linhas vazias
        if (linha_lida[0] == '#' || linha_lida[0] == '\0' || linha_lida[0] == '\n' || linha_lida[0] == '\r') {
            continue;
        }
        
        if (strncmp(linha_lida, "PASSO", 5) == 0) { 
            sscanf(linha_lida, "PASSO %d", &pacman->passo);
        } else if (strncmp(linha_lida, "POS", 3) == 0) {
            sscanf(linha_lida, "POS %d %d", &pacman->pos_x, &pacman->pos_y);
            defenicoes_lidas = 1; //todas as definições lidas
        }

        else if (defenicoes_lidas == 1 && pacman->n_moves < MAX_MOVES) {
            int move_index = pacman->n_moves;

            if (linha_lida[0]=='T'){
                sscanf(linha_lida, "T %d", &pacman->moves[move_index].turns); 
                pacman->moves[move_index].command = 'T';
                pacman->moves[move_index].turns_left = pacman->moves[move_index].turns; 
            } else { // movimentos
                pacman-> moves[move_index].command = linha_lida[0];
                pacman-> moves[move_index].turns = 1;
                pacman->moves[move_index].turns_left = 1; 
            }

            pacman->n_moves++;
        }
    }
    pacman->points = points;

    close(pacman_fd);

    //adicionar o pacman ao tabuleiro
    board->board[pacman->pos_y * board->width + pacman->pos_x].content = 'P';

    printf("DEBUG: Li %d movimentos do ficheiro %s\n", pacman->n_moves, pacman_path);

    return 0;
}

int load_ghost(board_t* board, const char* ghost_path, int ghost_index) {
    //obter a referencia para o ghost
    ghost_t* ghost = &board->ghosts[ghost_index];
    ghost -> n_moves = 0;
    ghost->passo = 0;         
    ghost->current_move = 0;  
    ghost->waiting = 0;       
    ghost->charged = 0;

    char linha_lida[1024];
    int defenicoes_lidas = 0; //flag para saber quando começar a ler os movimentos

    //abrir o ficheiro .m
    int ghost_fd = open(ghost_path, O_RDONLY);
    if (ghost_fd < 0) {
        return 1;
    }

    while (ler_linha(ghost_fd, linha_lida, sizeof(linha_lida)) > 0) {
        
        if (linha_lida[0] == '#' || linha_lida[0] == '\0' || linha_lida[0] == '\n' || linha_lida[0] == '\r') {
            continue;
        }
        
        
        if (strncmp(linha_lida, "PASSO", 5) == 0) { 
            sscanf(linha_lida, "PASSO %d", &ghost->passo);
        } else if (strncmp(linha_lida, "POS", 3) == 0) {
            sscanf(linha_lida, "POS %d %d", &ghost->pos_x, &ghost->pos_y);
            defenicoes_lidas = 1; //todas as definições lidas
        }

        //leitura de movimentos
        else if (defenicoes_lidas == 1 && ghost->n_moves < MAX_MOVES) {
            int move_index = ghost->n_moves;

            if (linha_lida[0]=='T'){
                sscanf(linha_lida, "T %d", &ghost->moves[move_index].turns); 
                ghost->moves[move_index].command = 'T';
                ghost->moves[move_index].turns_left = ghost->moves[move_index].turns; 
            } else { //comandos 
                ghost-> moves[move_index].command = linha_lida[0];
                ghost-> moves[move_index].turns = 1;
                ghost->moves[move_index].turns_left = 1; 
            }

            ghost->n_moves++;
        }
    }

    close(ghost_fd);

    //adicionar o ghost ao tabuleiro
    board->board[ghost->pos_y * board->width + ghost->pos_x].content = 'M';    
    return 0;
}


int load_level(board_t *board, const char* level_path, const char* level_dir_path, int accumulated_points) {

    int level_fd = open(level_path, O_RDONLY);
    if (level_fd < 0) {
        perror("Erro a abrir ficheiro .lvl");
        return -1;
    }

    char linha_lida[1024];
    int y_mapa = 0;// contador para a linha (y) atual em construção do mapa

    board->n_pacmans = 0;
    board->n_ghosts = 0;
    board->board = NULL; 

    // ler o ficheiro .lvl linha a linha
    while (ler_linha(level_fd, linha_lida, sizeof(linha_lida)) > 0){

        if (linha_lida[0] == '#') {
            continue;
        }
        
        if (strncmp(linha_lida, "DIM", 3) == 0) {
            sscanf(linha_lida, "DIM %d %d", &board->height, &board->width);
            board->board = calloc(board->width * board->height, sizeof(board_pos_t));

            if (board->board == NULL) {
                close(level_fd);
                return -1; 
            }

        } else if (strncmp(linha_lida, "TEMPO", 5) == 0) {
            sscanf(linha_lida, "TEMPO %d", &board->tempo);

        } else if (strncmp(linha_lida, "PAC", 3) == 0) {
            char nome_ficheiro_pac[256];
            sscanf(linha_lida, "PAC %s", nome_ficheiro_pac);

            char caminho_pac[512];
            // assim usamos a diretoria e nao apenas o ficheiro
            snprintf(caminho_pac, sizeof(caminho_pac), "%s/%s", level_dir_path, nome_ficheiro_pac);

            board->n_pacmans = 1; 
            board->pacmans = calloc(1, sizeof(pacman_t));
            if (board->pacmans == NULL) { 
                close(level_fd); 
                return -1; 
            }

            load_pacman(board, caminho_pac, accumulated_points);

        } else if (strncmp(linha_lida, "MON", 3) == 0) {

            board->n_ghosts = 0; //o num de fantasmas reinicia em cada nivel
            board->ghosts = calloc(MAX_GHOSTS, sizeof(ghost_t)); 

            if (board->ghosts == NULL) { 
                close(level_fd); 
                return -1; 
            }

            char *tok = strtok(linha_lida + 4, " "); // pulamos o "MON "

            while (tok != NULL && board->n_ghosts < MAX_GHOSTS) {
                char caminho_ghost[512];

                snprintf(caminho_ghost, sizeof(caminho_ghost), "%s/%s", level_dir_path, tok);

                load_ghost(board, caminho_ghost, board->n_ghosts);
                board->n_ghosts++;

                tok = strtok(NULL, " ");
            }
        
        } else if (board->board != NULL) { //verifica se o comando DIM foi lido
            

            if (y_mapa < board->height) {
                int tamanho_linha = strlen(linha_lida);
                for (int x = 0; x < board->width; x++) {
                    if (x >= tamanho_linha) break; 

                    int idx = y_mapa * board->width + x;
                    switch (linha_lida[x]) {
                        case 'X':
                            board->board[idx].content = 'W';
                            break;
                        case 'o':
                            board->board[idx].content = ' ';
                            board->board[idx].has_dot = 1;
                            break;
                        case '@':
                            board->board[idx].content = ' ';
                            board->board[idx].has_portal = 1;
                            break;
                        default: //caso para quando são espaços vazios
                            board->board[idx].content = ' ';
                            break;
                    }
                }
                y_mapa++; //próxima linha do mapa    
            }
        }
    } 

    close(level_fd);

    if (board->n_pacmans == 0) { //caso não haja um comando "PAC"
        board->n_pacmans = 1;
        board->pacmans = calloc(1, sizeof(pacman_t));
        if (board->pacmans == NULL) {
            return -1; 
        }
        load_pacman(board, NULL, accumulated_points);
    }
    
    return 0; 
}

void unload_level(board_t * board) {
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}
