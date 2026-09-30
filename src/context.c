#include "context.h"
#include <stdio.h>
#include <strings.h>

request_classification new_http_request = {.key = REQUEST_TYPE_HTTP};
request_classification ws_data = {.key = REQUEST_TYPE_WEBSOCKET_PAYLOAD};

context new_context(int s, request *req) {
  return (context){.socket = s,
                   .state = CONN_HTTP,
                   .req = req,
                   .out_len = 0,
                   .out_sent = 0,
                   .out = NULL,
                   .closed = false,
                   .payload = new_payload()};
}

void cleanup_context(context *ctx) {
  free_request(ctx->req);
  free(ctx->payload);
  free(ctx);
}

request_classification classify_request(context *ctx) {
  if (ctx->state == CONN_WS) {
    return ws_data;
  }
  request *req = ctx->req;
  const char *upgrade = header_get(&req->headers, "upgrade");
  if (upgrade == NULL) {
    return new_http_request;
  }
  if (strcasecmp(upgrade, "websocket") != 0) {
    return new_http_request;
  }
  const char *connection = header_get(&req->headers, "connection");
  if (connection == NULL) {
    return new_http_request;
  }

  if (strcasecmp(connection, "upgrade") != 0) {
    return new_http_request;
  }
  const char *version = header_get(&req->headers, "sec-websocket-version");
  if (strcasecmp(version, "13") != 0) {
    perror("Invalid websocket version");
    return new_http_request;
  }
  const char *key = header_get(&req->headers, "sec-websocket-key");
  return (request_classification){.key = REQUEST_TYPE_WEBSOCKET_UPGRADE,
                                  .websocket_key = key};
}
