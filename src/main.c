#include "parser.h"
#include "request.h"
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENTS 128

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;

typedef enum {
  RESPONSE_BUILDING,
  RESPONSE_READY,
} response_state;

typedef enum {
  RECEIVE_ERROR,
  RECEIVE_DONE,
  RECEIVE_CONTINUE,
  RECEIVE_CLOSED
} receive_signal;

receive_signal receive(int fd, request *req) {
  payload p = {.buf = {0}, .i = 0, .bytes_read = 0};
  p.bytes_read = recv(fd, p.buf, BUF_SIZE, 0);
  if (p.bytes_read == -1) {
    if (errno == EAGAIN)
      return RECEIVE_CONTINUE;
    if (errno == ECONNRESET)
      return RECEIVE_CLOSED;
  }
  if (p.bytes_read == -1) {
    printf("Failed to recv data into a buffer %s\n", strerror(errno));
    return RECEIVE_ERROR;
  }
  if (p.bytes_read == 0) {
    return RECEIVE_CLOSED;
  }
  switch (parse(&p, req)) {
  case PARSE_ERROR:
    return RECEIVE_ERROR;
  case PARSE_DONE:
    return RECEIVE_DONE;
  case PARSE_CONTINUE:
    return RECEIVE_CONTINUE;
  }
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
const char *__asan_default_options(void) { return "detect_leaks=1"; }
#endif
#endif

typedef struct {
  request *req;
  char *out;
  size_t out_len;
  size_t out_sent;
} ctx;

static void reset_ctx(ctx *c) {
  *c->req = new_req();
  c->out_len = 0;
  c->out_sent = 0;
}

static void cleanup_ctx(ctx *c) {
  free_request(c->req);
  free(c);
}

static void cleanup_socket(int s, ctx *c) {
  close(s);
  cleanup_ctx(c);
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr = {.sin_family = AF_INET,
                      .sin_port = htons(80),
                      .sin_addr = {.s_addr = htonl(INADDR_ANY)}};

  // making sockets nonblocking
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

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
  if (listen(fd, SOMAXCONN) != 0) {
    printf("Failed to listen to socket\n");
  }

  struct kevent ev;
  int kq;
  if ((kq = kqueue()) == -1) {
    perror("kqueue");
    exit(EXIT_FAILURE);
  }

  // register socket read events
  EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
  if (kevent(kq, &ev, 1, NULL, 0, NULL) == -1) {
    perror("kevent");
    exit(EXIT_FAILURE);
  }

  // register sigint for cleanup
  signal(SIGINT, SIG_IGN);
  EV_SET(&ev, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  if (kevent(kq, &ev, 1, NULL, 0, NULL) == -1) {
    perror("kevent signal handler");
    exit(EXIT_FAILURE);
  }

  struct kevent event[MAX_EVENTS];
  for (;;) {
    int new_events = kevent(kq, NULL, 0, event, MAX_EVENTS, NULL);
    for (size_t i = 0; i < new_events && new_events > 0; i++) {
      int event_fd = event[i].ident;
      if (/* sigint */ event[i].filter == EVFILT_SIGNAL && event_fd == SIGINT) {
        printf("SIGINT; Shutting down\n");
        goto shutdown;
      } else if (event_fd == fd) {
        int s = accept(event_fd, NULL, NULL);
        if (s == -1) {
          if (errno == EINTR) {
            close(s);
            continue;
          }
          printf("Could not accept socket connection %s\n", strerror(errno));
          continue;
        }
        fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
        request *req = malloc(sizeof(request));
        *req = new_req();
        ctx *c = malloc(sizeof(ctx));
        *c = (ctx){.req = req, .out_len = 0, .out_sent = 0, .out = NULL};
        EV_SET(&ev, s, EVFILT_READ, EV_ADD, 0, 0, c);
        if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0) {
          perror("kevent error");
        }
      } else if (event[i].filter == EVFILT_READ) {
        ctx *c = (ctx *)event[i].udata;
        request *req = c->req;
        receive_signal result = receive(event_fd, req);
        if (result == RECEIVE_CLOSED) {
          cleanup_socket(event_fd, c);
        } else if (result == RECEIVE_ERROR) {
          cleanup_socket(event_fd, c);
        } else if (result == RECEIVE_DONE) {
          ctx *c = (ctx *)event[i].udata;
          ssize_t out_len = 113;
          char *out = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html \r\n"
                      "Content-Length: 47 \r\n\r\n"
                      "<!DOCTYPE html>\n"
                      "<body><div>hi lol</div></body>\n";
          ssize_t sent =
              send(event_fd, out + c->out_sent, out_len - c->out_sent, 0);
          if (sent == -1) {
            printf("Could not send result, closing socket\n");
            cleanup_socket(event_fd, c);
            continue;
          }
          // TODO: control for backpressure by switching read/write modes
          c->out_sent += sent;
          print_request(req);
          // printf("Sent response!\n");
          if (c->req->version_number == HTTP_1_0) {
            cleanup_socket(event_fd, c);
          } else {
            free_request(c->req);
            reset_ctx(c);
          }
        };
      } else if (event[i].flags & EV_EOF) {
        printf("Client disconnected\n");
        ctx *c = (ctx *)event[i].udata;
        if (c) {
          free_request(c->req);
          free(c);
        }
        close(event_fd);
      }
    }
  }

shutdown:
  close(kq);
  close(fd);
  return 0;
}
