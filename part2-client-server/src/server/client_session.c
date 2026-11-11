#include "client_session.h"
#include "protocol.h"

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

//inicializa as variáveis
void init_session(PlayerSession *sessions, int max_games) {
    for (int i = 0; i < max_games; i++) {
        sessions[i].active = false;

        sessions[i].score = 0;
        sessions[i].client_id = -1;

        sessions[i].notif_pipe_fd = -1;
        sessions[i].req_pipe_fd = -1;

        if (pthread_mutex_init(&sessions[i].lock, NULL) != 0) {
            perror("[ERRO] Falha ao iniciar mutex da sessão");
        }
    }
}

//chamada pela thread gestora quando recebe um cliente
//atribui valores vindos dos pipes aos clientes
void activate_session(PlayerSession *session, int client_id, int notif_fd, int req_fd, char* req_path, char* notif_path) {
    pthread_mutex_lock(&session->lock);
    
    session->client_id = client_id;
    session->active = true;
    session->score = 0;
    
    session->notif_pipe_fd = notif_fd;
    session->req_pipe_fd = req_fd;
    strncpy(session->req_pipe_path, req_path, MAX_PIPE_PATH_LENGTH);
    strncpy(session->notif_pipe_path, notif_path, MAX_PIPE_PATH_LENGTH);
    
    pthread_mutex_unlock(&session->lock);
    
    printf("[Sessão] Cliente %d ativado (ReqFD: %d, NotifFD: %d)\n", client_id, req_fd, notif_fd);
}

//chamada dentro de play_game_session
void update_score(PlayerSession *session, int new_score) {
    pthread_mutex_lock(&session->lock);
    
    session->score = new_score;
    
    pthread_mutex_unlock(&session->lock);
}

//chamada pela thread gestora quando o jogo acaba ou cliente desconecta
void close_session(PlayerSession *session) {
    pthread_mutex_lock(&session->lock);
    
    if (session->active) {
        
        if (session->notif_pipe_fd != -1) {
            close(session->notif_pipe_fd);
            session->notif_pipe_fd = -1;
        }
        
        if (session->req_pipe_fd != -1) {
            close(session->req_pipe_fd); 
            session->req_pipe_fd = -1;
        }

        printf("[Sessão] Cliente %d removido. Pontuação Final: %d\n", 
               session->client_id, session->score);
               
        session->active = false;
        session->client_id = -1;
        session->score = 0;
    }
    
    pthread_mutex_unlock(&session->lock);
}