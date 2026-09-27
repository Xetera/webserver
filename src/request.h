#pragma once

#include "headers.h"
#include "parse_state.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  HTTP_UNKNOWN = 0,
  HTTP_1_1,
} protocol_version;

typedef struct {
  parse_state state;
  header_list headers;
  bool expect_new_header;
  request_line_parse_state rl_state;
  header_line_parse_state hl_state;
  body_parse_state b_state;
  size_t body_bytes_read;
  size_t body_bytes_remaining;
  char *method;
  char *path;
  char *version;
  char *body;
  protocol_version version_number;
} request;

typedef struct {
  // extra space for the \0
  char buf[BUF_SIZE + 1];
  size_t start;
  size_t i;
  size_t bytes_read;
} payload;

request new_req();
void free_request(request *req);
void print_request(request *req);
