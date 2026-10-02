#pragma once

#include "headers.h"
#include "parse_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_ssize_t.h>

typedef enum {
  HTTP_UNKNOWN = 0,
  HTTP_1_0,
  HTTP_1_1,
} protocol_version;

typedef struct {
  parse_state parse_state;
  header_list headers;
  bool expect_new_header;
  request_line_parse_state rl_state;
  header_line_parse_state hl_state;
  size_t body_bytes_read;
  size_t body_bytes_remaining;
  char *method;
  char *path;
  char *version;
  char *body;
  protocol_version version_number;
} request;

typedef enum request_failure_reasons {
  REQUEST_FAILURE_MULTIPLE_HOST_HEADERS = 1,
  REQUEST_FAILURE_MISSING_HOST_HEADER = 2,
  REQUEST_FAILURE_NEGATIVE_CONTENT_LENGTH = 3,
  REQUEST_FAILURE_NON_NUMERIC_CONTENT_LENGTH = 4,
} request_failure_reasons;

request new_req();
void reset_request(request *req);
void free_request(request *req);
void print_request(request *req);
int handle_request_inconsistencies(request *req);
