#pragma once

#include "headers.h"
typedef enum {
  STATUS_101,
  STATUS_200,
  STATUS_400,
  STATUS_404,
  STATUS_426,
  STATUS_500,
} response_status;

typedef struct {
  response_status status;
  header_list headers;
  char *body;
  size_t body_len;
} response;

response new_res();
const char *serialize_response(response *response, size_t *out_length);
