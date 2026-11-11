#include "api.h"
#include "protocol.h"
#include "io.h"
#include "debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>



struct Session {
  int id;
  int req_pipe_fd;
  int notif_pipe_fd;
  char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

static struct Session session = {
    .req_pipe_fd = -1,
    .notif_pipe_fd = -1
};

int create_pipe(char const *filepath) {
    if (unlink(filepath) != 0 && errno != ENOENT) {
        perror("[ERR]: unlink failed");
        return 1;
    }
    //pipe com permissoes rw-r
    if (mkfifo(filepath, 0640) != 0) {
        perror("[ERR]: mkfifo failed");
        return 1;
    }
    return 0;
}

int delete_pipe(char const *filepath) {
    if (unlink(filepath) != 0 && errno != ENOENT) {
        perror("[ERR]: unlink failed");
        return 1;
    }
    return 0;
}


int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {
    memset(session.req_pipe_path, 0, sizeof(session.req_pipe_path));
    memset(session.notif_pipe_path, 0, sizeof(session.notif_pipe_path));
    
    strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
    strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);

    if (create_pipe(session.req_pipe_path) != 0) return 1;
    if (create_pipe(session.notif_pipe_path) != 0) {
        delete_pipe(session.req_pipe_path);
        return 1;
    }

    int server_pipe_fd = open(server_pipe_path, O_WRONLY);
    if (server_pipe_fd == -1) {
        perror("[ERRO] Falha ao abrir o pipe do servidor (O servidor está a correr?)");
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }
    //criação mensagem de conexão para enviar ao server
    char connect_message[1 + MAX_PIPE_PATH_LENGTH + MAX_PIPE_PATH_LENGTH];
    memset(connect_message, 0, sizeof(connect_message));

    connect_message[0] = (char) OP_CODE_CONNECT;
    strncpy(connect_message + 1, session.req_pipe_path, MAX_PIPE_PATH_LENGTH);
    strncpy(connect_message + 1 + MAX_PIPE_PATH_LENGTH, session.notif_pipe_path, MAX_PIPE_PATH_LENGTH);

    if (write_all(server_pipe_fd, connect_message, sizeof(connect_message)) == -1) {
        perror("[ERRO] Falha ao enviar mensagem de conexão");
        close(server_pipe_fd);
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }
    
    close(server_pipe_fd); 

    printf("[INFO] Pedido enviado\n");

    session.notif_pipe_fd = open(session.notif_pipe_path, O_RDONLY);
    if (session.notif_pipe_fd == -1) {
        perror("[ERRO] Falha ao abrir pipe de notificações para leitura");
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }
    //leitura da resposta do server
    char server_response[2]; 
    if (read_all(session.notif_pipe_fd, server_response, sizeof(server_response), NULL) <= 0) {
        perror("[ERRO] Falha ao ler resposta do servidor");
        close(session.notif_pipe_fd);
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }

    if (server_response[1] != 0) {
        printf("[ERRO] O servidor recusou a conexão. Código de erro: %d\n", server_response[1]);
        close(session.notif_pipe_fd);
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }

    printf("[SUCESSO] conectado!\n");

    session.req_pipe_fd = open(session.req_pipe_path, O_WRONLY);
    if (session.req_pipe_fd == -1) {
        perror("[ERRO] Falha ao abrir pipe de pedidos");
        close(session.notif_pipe_fd);
        delete_pipe(session.req_pipe_path);
        delete_pipe(session.notif_pipe_path);
        return 1;
    }

    printf("[SUCESSO] Tudo pronto. O jogo vai começar!\n");
    return 0;
}

int pacman_disconnect(void) {
    char msg[1];
    msg[0] = OP_CODE_DISCONNECT;
    
    if (session.req_pipe_fd != -1) {
        write_all(session.req_pipe_fd, msg, 1);
        close(session.req_pipe_fd);
    }

    if (session.notif_pipe_fd != -1) {
        close(session.notif_pipe_fd);
    }
    
    delete_pipe(session.req_pipe_path);
    delete_pipe(session.notif_pipe_path);
    
    return 0;
}

void pacman_play(char command) {
    char msg[2];
    msg[0] = OP_CODE_PLAY;
    msg[1] = command;

    if (write_all(session.req_pipe_fd, msg, sizeof(msg)) == -1) {
        perror("Failed to send play command");
    }
}

Board receive_board_update(void) {
    Board board = {0};
    
    char op_code;
    if (read_all(session.notif_pipe_fd, &op_code, sizeof(char), NULL) <= 0) {
        board.game_over = 1; 
        return board;
    }

    if (op_code != OP_CODE_BOARD) {
        board.game_over = 1;
        return board;
    }

    int params[6];
    if (read_all(session.notif_pipe_fd, params, sizeof(params), NULL) <= 0) {
        board.game_over = 1;
        return board;
    }
    //desenho do board recebido pelo notif pipe
    board.width = params[0];
    board.height = params[1];
    board.tempo = params[2];
    board.victory = params[3];
    board.game_over = params[4];
    board.accumulated_points = params[5];

    int data_size = board.width * board.height;
    board.data = malloc(data_size * sizeof(char));

    if (read_all(session.notif_pipe_fd, board.data, data_size, NULL) <= 0) {
        free(board.data);
        board.data = NULL;
        board.game_over = 1;
    }

    return board;
}