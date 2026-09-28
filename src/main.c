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
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// create
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

receive_signal receive(int fd, payload *p, request *req,
                       void (*on_request_finish)(request *req, int socket)) {
  size_t to_read = BUF_SIZE - p->i;
  p->bytes_read = recv(fd, &p->buf[p->i], to_read, 0);
  if (p->bytes_read == -1) {
    if (errno == EAGAIN)
      return RECEIVE_CONTINUE;
    if (errno == ECONNRESET)
      return RECEIVE_CLOSED;
  }
  if (p->bytes_read == -1) {
    printf("Failed to recv data into a buffer %s\n", strerror(errno));
    return RECEIVE_ERROR;
  }
  if (p->bytes_read == 0) {
    return RECEIVE_CLOSED;
  }

  receive_signal signal;
  // need to loop this to support pipelining
  do {
    switch (parse(p, req)) {
    case PARSE_ERROR:
      return RECEIVE_ERROR;
    case PARSE_DONE:
      signal = RECEIVE_DONE;
      on_request_finish(req, fd);
      // there may be the start of a new request
      // already lined up in this buffer
      reset_request(req);
      break;
    case PARSE_CONTINUE:
      signal = RECEIVE_CONTINUE;
      continue;
      // ...
    }
  } while (p->i < p->bytes_read);
  p->i = 0;
  p->bytes_read = 0;
  return signal;
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
const char *__asan_default_options(void) { return "detect_leaks=1"; }
#endif
#endif

typedef struct {
  request *req;
  payload *payload;
  char *out;
  size_t out_len;
  size_t out_sent;
  bool closed;
} ctx;

payload *new_payload() {
  payload *p = malloc(sizeof(payload));
  memset(p->buf, 0, BUF_SIZE + 1);
  p->bytes_read = 0;
  p->i = 0;
  return p;
}

// static void reset_ctx(ctx *c) {
//   *c->req = new_req();
//   c->out_len = 0;
//   c->out_sent = 0;
//   c->out = NULL;
//   c->closed = false;
//   c->payload = new_payload();
// }

static void cleanup_ctx(ctx *c) {
  free_request(c->req);
  free(c->payload);
  free(c);
  // c->closed = true;
}

static void cleanup_socket(int socket, ctx *c) {
  close(socket);
  cleanup_ctx(c);
}

static void on_request(request *req, int socket) {
  ssize_t out_len = 113;
  char *out = "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html \r\n"
              "Content-Length: 47 \r\n\r\n"
              "<!DOCTYPE html>\n"
              "<body><div>hi lol</div></body>\n";
  send(socket, out, out_len, 0);
}

static void on_socket_connect(int socket, struct kevent *event, int kq,
                              struct kevent *ev) {
  int s = accept(socket, NULL, NULL);
  if (s == -1) {
    if (errno == ECONNABORTED) {
      return;
    }
    if (errno == EWOULDBLOCK) {
      return;
    }
    printf("Could not accept socket connection %s\n", strerror(errno));
    return;
  }
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
  request *req = malloc(sizeof(request));
  *req = new_req();
  ctx *c = malloc(sizeof(ctx));
  *c = (ctx){.req = req,
             .out_len = 0,
             .out_sent = 0,
             .out = NULL,
             .closed = false,
             .payload = new_payload()};
  EV_SET(ev, s, EVFILT_READ, EV_ADD, 0, 0, c);
  if (kevent(kq, ev, 1, NULL, 0, NULL) < 0) {
    perror("kevent error");
  }
}

static void on_socket_receive(int socket, struct kevent *event) {
  ctx *c = (ctx *)event->udata;
  if ((event->flags & EV_EOF) && event->data == 0) {
    cleanup_socket(socket, c);
    return;
  }
  switch (receive(socket, c->payload, c->req, on_request)) {
  case RECEIVE_CLOSED:
  case RECEIVE_ERROR:
    cleanup_socket(socket, c);
    break;
  case RECEIVE_DONE:
    // HTTP/1.0 connections are not persistent
    if (c->req->version_number == HTTP_1_0) {
      cleanup_socket(socket, c);
    }
    break;
  case RECEIVE_CONTINUE:
    break;
  }
}

static void run_worker(int fd) {
  printf("Running worker %c\n", fd);
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
      if (/* SIGINT */ event[i].filter == EVFILT_SIGNAL && event_fd == SIGINT) {
        goto shutdown;
      } else if (event_fd == fd) {
        on_socket_connect(event_fd, &event[i], kq, &ev);
      } else if (event[i].filter == EVFILT_READ) {
        on_socket_receive(event_fd, &event[i]);
      }
    }
  }
shutdown:
  printf("SIGINT; Shutting down\n");
  close(kq);
  close(fd);
  exit(EXIT_SUCCESS);
}

static bool setopts(int socket) {
  int opt = 1;
  if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
    printf("could not set opts %s\n", strerror(errno));
    return false;
  }
  if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt)) != 0) {
    printf("could not set opts %s\n", strerror(errno));
    return false;
  }
  return true;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr = {.sin_family = AF_INET,
                      .sin_port = htons(80),
                      .sin_addr = {.s_addr = htonl(INADDR_ANY)}};

  // making sockets nonblocking
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  if (!setopts(fd)) {
    return 1;
  }

  int bind_result = bind(fd, (sockaddr *)&addr, sizeof(addr));
  if (bind_result != 0) {
    printf("Failed to listen to bind to socket %s \n", strerror(errno));
    return 0;
  }
  if (listen(fd, SOMAXCONN) != 0) {
    printf("Failed to listen to socket\n");
  }

#ifdef DEBUG
  printf("Running in debug mode\n");
  run_worker(fd);
#else
  // extra workers don't seem to increase capacity by any amount?
  int count = 2;
  // size_t count_len = sizeof(count);
  // sysctlbyname("hw.perflevel0.logicalcpu", &count, &count_len, NULL, 0);
  for (size_t worker = 0; worker < count; worker++) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    }
    if (pid == 0) {
      run_worker(fd);
    }
  }
  close(fd);
  while (wait(NULL) > 0)
    ;
#endif

  return 0;
}
