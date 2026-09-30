#pragma once

#include "request.h"
#include "websocket.h"

typedef enum { CONN_HTTP, CONN_WS, CONN_CLOSING } conn_state;

typedef struct context {
  int socket;
  conn_state state;
  request *req;
  websocket *ws;
  payload *payload;
  char *out;
  size_t out_len;
  size_t out_sent;
  bool closed;
} context;

typedef enum {
  REQUEST_TYPE_UNKNOWN = 0,
  REQUEST_TYPE_HTTP,
  REQUEST_TYPE_WEBSOCKET_UPGRADE,
  REQUEST_TYPE_WEBSOCKET_PAYLOAD
} request_classification_type;

typedef struct {
  request_classification_type key;
  union {
    const char *websocket_key;
  };
} request_classification;

request_classification classify_request(context *);
context new_context(int, request *);

void cleanup_context(context *ctx);
