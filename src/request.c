#include "request.h"
#include "payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

request new_req() {
  return (request){.parse_state = START,
                   .headers = new_header_list(),
                   .expect_new_header = true,
                   .rl_state = RL_START,
                   .hl_state = HL_KEY,
                   .body_bytes_read = 0,
                   .body_bytes_remaining = -1,
                   .method = NULL,
                   .path = NULL,
                   .version = NULL,
                   .version_number = HTTP_UNKNOWN};
}

void free_request(request *req) {
  free_header_list(&req->headers);
  free(req->method);
  free(req->path);
  free(req->version);
  free(req->body);
}

void reset_request(request *req) {
  free_request(req);
  *req = new_req();
}

void print_request(request *req) {
  printf("%s %s %s\n", req->method, req->path, req->version);
  print_headers(&req->headers);
}

payload *new_payload() {
  payload *p = (payload *)malloc(sizeof(payload));
  memset(p->buf, 0, BUF_SIZE + 1);
  p->bytes_read = 0;
  p->i = 0;
  return p;
}
