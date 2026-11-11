#ifndef CLIENT_SESSION_H
#define CLIENT_SESSION_H

#include <pthread.h>

typedef struct {
    int client_id;           
    int active;              
    int score;               
    int notif_pipe_fd;   
    int req_pipe_fd;

    char req_pipe_path[40];    
    char notif_pipe_path[40]; 
    pthread_mutex_t lock;    
} PlayerSession;

void init_session(PlayerSession *session, int id);

void close_session(PlayerSession *session);

void activate_session(PlayerSession *session, int client_id, int notif_fd, int req_fd, char* req_path, char* notif_path);

void update_score(PlayerSession *session, int new_score);

#endif