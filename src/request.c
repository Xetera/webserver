#include "request.h"
#include "payload.h"
#include <errno.h>
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

int handle_request_inconsistencies(request *req) {
  int host_count = 0;
  char *content_length = NULL;
  for (size_t i = 0; i < req->headers.len; i++) {
    header *h = &req->headers.items[i];
    if (strcasecmp(h->key, "host") == 0) {
      host_count++;
    }
    if (strcasecmp(h->key, "content-length") == 0) {
      content_length = h->value;
    }
  }
  if (content_length != NULL) {
    char *endptr;
    errno = 0;
    long n = strtol(content_length, &endptr, 10);
    if (endptr == content_length || *endptr != '\0' || errno == ERANGE) {
      return REQUEST_FAILURE_NON_NUMERIC_CONTENT_LENGTH;
    }
    if (n < 0) {
      return REQUEST_FAILURE_NEGATIVE_CONTENT_LENGTH;
    }
  }
  if (host_count == 0) {
    return REQUEST_FAILURE_MISSING_HOST_HEADER;
  }
  if (host_count > 1) {
    return REQUEST_FAILURE_MULTIPLE_HOST_HEADERS;
  }

  return 0;
}
