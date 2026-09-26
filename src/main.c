#include "parser.h"
#include "request.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

typedef enum {
  RESPONSE_BUILDING,
  RESPONSE_READY,
} response_state;

parse_signal receive(int fd, request *req) {
  payload p = {.buf = {0}, .i = 0};
  p.bytes_read = recv(fd, p.buf, BUF_SIZE, 0);
  if (p.bytes_read == -1) {
    printf("Failed to recv data into a buffer %s\n", strerror(errno));
    return PARSE_ERROR;
  }
  printf("BUFFER RECEIVED (read=%zd) [%d] (%s)\n", p.bytes_read, BUF_SIZE,
         p.buf);
  return parse(&p, req);
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr = {.sin_family = AF_INET,
                      .sin_port = htons(80),
                      .sin_addr = {.s_addr = htonl(INADDR_ANY)}};
  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
    printf("could not set opts %s\n", strerror(errno));
    return 0;
  }
  int bind_result = bind(fd, (sockaddr *)&addr, sizeof(addr));
  if (bind_result != 0) {
    printf("Failed to listen to bind to socket %s \n", strerror(errno));
    return 0;
  }
  if (listen(fd, 5) != 0) {
    printf("Failed to listen to socket\n");
  }
  printf("Listening...\n");
  bool working = true;
  while (working) {
    int s = accept(fd, NULL, NULL);
    request req = new_req();
    if (s == -1) {
      printf("Could not accept socket connection %s\n", strerror(errno));
      continue;
    }
    printf("Accepting!\n");
    while (1) {
      parse_signal result = receive(s, &req);
      if (result == PARSE_ERROR) {
        close(s);
        working = false;
        break;
      }
      if (result == PARSE_DONE) {
        send(s,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html \r\n"
             "Content-Length: 47 \r\n\r\n"
             "<!DOCTYPE html>\n"
             "<body><div>hi lol</div></body>\n",
             114, 0);
        print_request(&req);
        printf("Sent response!\n");
        close(s);
        free_request(&req);
        break;
      };
    }
  }
  return 0;
}
