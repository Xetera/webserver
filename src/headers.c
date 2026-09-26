#include "headers.h"
#include <stdio.h>
#include <stdlib.h>

void ensure_capacity(header_list *hl) {
  if (hl->len >= hl->capacity) {
    hl->capacity *= 2;
    header *new_list = malloc(sizeof(header) * hl->capacity);
    for (size_t i = 0; i < hl->len; i++) {
      new_list[i] = hl->items[i];
    }
    // does this break other references? idk
    free(hl->items);
    hl->items = new_list;
  }
}

header *new_header(header_list *hl) {
  ensure_capacity(hl);
  size_t at = hl->len;
  hl->items[at] = (header){.key = "", .value = ""};
  hl->len++;
  return &hl->items[at];
}

bool headers_empty(header_list *hl) { return hl->len == 0; }
header *last_header(header_list *hl) { return &hl->items[hl->len - 1]; }

header_list new_header_list() {
  return (header_list){.items =
                           malloc(sizeof(header) * HEADER_DEFAULT_CAPACITY),
                       .len = 0,
                       .capacity = HEADER_DEFAULT_CAPACITY};
}

void print_headers(header_list *hl) {
  for (size_t i = 0; i < hl->len; i++) {
    header h = hl->items[i];
    printf("%s: %s\n", h.key, h.value);
  }
}
