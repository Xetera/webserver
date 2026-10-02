#include "response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *serialize_status(response_status status) {
  switch (status) {
  case STATUS_101:
    return "101 Switching Protocols";
  case STATUS_200:
    return "200 OK";
  case STATUS_400:
    return "400 Bad Request";
  case STATUS_404:
    return "404 Not Found";
  case STATUS_426:
    return "426 Upgrade Required";
  case STATUS_500:
    return "404 Internal Server Error";
  }
}

const char *serialize_response(response *res, size_t *out_length) {
  char *status = serialize_status(res->status);
  size_t status_len = 11 + strlen(status);
  size_t headers_length = res->headers.len * 4;
  //                                [: \r\n] ^
  for (size_t i = 0; i < res->headers.len; i++) {
    header *h = &res->headers.items[i];
    size_t key_length = strlen(h->key);
    size_t value_length = strlen(h->value);
    headers_length += key_length + value_length;
  }

  size_t buffer_size = sizeof(char) * (status_len + headers_length +
                                       res->body_len + 2) /* final \r\n */;
  char *out = (char *)malloc(buffer_size + 1); // one extra for the \0
  *out_length = buffer_size;

  snprintf(out, status_len + 1, "HTTP/1.1 %s\r\n", status);
  size_t offset = status_len;
  for (size_t i = 0; i < res->headers.len; i++) {
    header *h = &res->headers.items[i];
    size_t key_length = strlen(h->key);
    size_t value_length = strlen(h->value);
    size_t combined_length = key_length + value_length + 4;
    snprintf(out + offset, combined_length + 1, "%s: %s\r\n", h->key, h->value);
    offset += combined_length;
  }
  if (res->body_len) {
    memcpy(out + offset, res->body, res->body_len);
    offset += res->body_len;
  }
  memcpy(out + offset, "\r\n\0", 3);

  return out;
}

response new_res() {
  return (response){.body = NULL,
                    .body_len = 0,
                    .headers = new_header_list(),
                    .status = STATUS_500};
}
