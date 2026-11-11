CC = gcc

CFLAGS = -g -Wall -Wextra -std=c17 -D_POSIX_C_SOURCE=200809L -I src/common
LDFLAGS = -lncurses

SRC_COMMON = src/common
SRC_CLIENT = src/client
SRC_SERVER = src/server

OBJ_DIR = obj
BIN_DIR = bin

CLIENT_EXE = $(BIN_DIR)/client
SERVER_EXE = $(BIN_DIR)/server

CLIENT_SRCS = $(SRC_CLIENT)/client_main.c \
							$(SRC_CLIENT)/api.c \
							$(SRC_CLIENT)/display.c \
							$(SRC_CLIENT)/debug.c \
							$(SRC_COMMON)/io.c

SERVER_SRCS = $(SRC_SERVER)/game.c \
							$(SRC_SERVER)/board.c \
							$(SRC_SERVER)/display.c \
							$(SRC_SERVER)/parser.c \
							$(SRC_SERVER)/client_session.c \
							$(SRC_COMMON)/io.c

CLIENT_OBJS = $(patsubst %.c, $(OBJ_DIR)/client/%.o, $(notdir $(CLIENT_SRCS)))
SERVER_OBJS = $(patsubst %.c, $(OBJ_DIR)/server/%.o, $(notdir $(SERVER_SRCS)))

.PHONY: all client server clean folders

all: folders client server

client: folders $(CLIENT_EXE)

server: folders $(SERVER_EXE)

$(CLIENT_EXE): $(CLIENT_OBJS)
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
		@echo "client compiled at: $@"

$(SERVER_EXE): $(SERVER_OBJS)
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
		@echo "server compiled at: $@"

$(OBJ_DIR)/client/%.o: $(SRC_CLIENT)/%.c
		$(CC) $(CFLAGS) -I $(SRC_CLIENT)/include -c $< -o $@

$(OBJ_DIR)/server/%.o: $(SRC_SERVER)/%.c
		$(CC) $(CFLAGS) -I $(SRC_SERVER)/include -c $< -o $@

$(OBJ_DIR)/client/io.o: $(SRC_COMMON)/io.c
		$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/server/io.o: $(SRC_COMMON)/io.c
		$(CC) $(CFLAGS) -c $< -o $@

folders:
		@mkdir -p $(BIN_DIR)
		@mkdir -p $(OBJ_DIR)/client
		@mkdir -p $(OBJ_DIR)/server

# Limpa tudo
clean:
		rm -rf $(OBJ_DIR) $(BIN_DIR)
		@echo "all cleaned"