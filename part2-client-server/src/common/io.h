#ifndef COMMON_IO_H
#define COMMON_IO_H

#include <stddef.h>
#include <sys/types.h>

// Lê exatamente 'size' bytes. Retorna tamanho lido ou -1 em erro.
// 'intr' pode ser NULL. Se não for NULL, serve para detectar interrupções.
int read_all(int fd, void *buffer, size_t size, int *intr);

// Escreve exatamente 'size' bytes. Retorna tamanho escrito ou -1 em erro.
int write_all(int fd, const void *buffer, size_t size);

// Funções auxiliares de tempo e strings
int read_string(int fd, char *str);
void delay(unsigned int time_ms);

#endif // COMMON_IO_H