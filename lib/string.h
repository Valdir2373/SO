/*
 * lib/string.h — Funções de string bare-metal (sem libc)
 */
#ifndef _STRING_H
#define _STRING_H

#include <types.h>

size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strcat(char *dst, const char *src);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);

void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);

/* Converte inteiro para string decimal */
void    itoa(int val, char *buf, int base);
/* Converte string para inteiro */
int     atoi(const char *s);

/* Converte para maiúsculas/minúsculas */
int     toupper(int c);
int     tolower(int c);
int     isdigit(int c);
int     isalpha(int c);
int     isspace(int c);

#endif /* _STRING_H */
