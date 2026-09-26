#include "request.h"
#include <stdio.h>
#include <stdlib.h>

request new_req() {
  return (request){.state = START,
                   .headers = new_header_list(),
                   .expect_new_header = true,
                   .rl_state = RL_START,
                   .hl_state = HL_KEY,
                   .method = NULL,
                   .path = NULL,
                   .version = NULL,
                   .version_number = HTTP_UNKNOWN};
}

void free_request(request *req) { free_header_list(&req->headers); }

void print_request(request *req) {
  printf("%s %s %s\n", req->method, req->path, req->version);
  print_headers(&req->headers);
}
