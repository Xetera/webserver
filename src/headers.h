#pragma once

#include <stdbool.h>
#include <stddef.h>
#define HEADER_DEFAULT_CAPACITY 4

typedef struct {
  char *key;
  char *value;
} header;

typedef struct {
  header *items;
  size_t len;
  size_t capacity;
} header_list;

void ensure_capacity(header_list *hl);

header *new_header(header_list *hl);
const char *header_get(header_list *hl, const char *name);

bool headers_empty(header_list *hl);
header *last_header(header_list *hl);

header_list new_header_list();
void free_header_list(header_list *hl);

void print_headers(header_list *);
