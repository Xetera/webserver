#pragma once

#include "request.h"
#include <math.h>

#define CR '\r'
#define LF '\n'
#define SP ' '
#define MAX_BODY_SIZE pow(2, 13)

typedef struct {
  char *key;
  char *value;
} header_parse_line;

parse_signal parse_request_line(payload *p, request *req);
parse_signal parse_headers(payload *p, request *req);
parse_signal parse(payload *p, request *req);
