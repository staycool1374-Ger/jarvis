#pragma once

#include <sys/types.h>

#define NULL ((void*)0)

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void exit(int status);
void abort(void);
int atoi(const char* s);
long atol(const char* s);
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

int abs(int n);
long labs(long n);
