#pragma once
#include <stddef.h>
#include <sys/_types/_ssize_t.h>

#define BUF_SIZE 8192

typedef struct {
  // extra space for the \0
  char buf[BUF_SIZE + 1];
  size_t i;
  ssize_t bytes_read;
} payload;

payload *new_payload();
