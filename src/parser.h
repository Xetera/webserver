#pragma once

#include "request.h"

#define CR '\r'
#define LF '\n'
#define SP ' '

typedef struct {
  char *key;
  char *value;
} header_parse_line;

parse_signal parse_intro(payload *p, request *req);
parse_signal parse_headers(payload *p, request *req);
parse_signal parse(payload *p, request *req);
