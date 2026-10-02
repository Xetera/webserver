#include "context.h"
#include "parser.h"
#include "request.h"
#include "websocket.h"
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

#define MAX_EVENTS 128
#define WORKER_COUNT 8

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

void send_status(context *ctx, response_status status) {
  response res = new_res();
  res.status = status;
  size_t len;
  const char *result = serialize_response(&res, &len);
  send(ctx->socket, result, len, 0);
}

receive_signal receive(context *ctx, void (*on_request_finish)(context *ctx),
                       void (*on_websocket_receive)(context *ctx)) {
  payload *p = ctx->payload;
  request *req = ctx->req;
  int fd = ctx->socket;
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
  // even though literally no client pipelines requests for HTTP/1.1
  do {
    if (ctx->state == CONN_WS) {
      switch (parse_websocket(ctx->payload, ctx->ws)) {
      case PARSE_ERROR:
        return RECEIVE_ERROR;
      case PARSE_DONE:
        signal = RECEIVE_DONE;
        int inconsistency = handle_websocket_inconsistencies(ctx->ws);
        if (inconsistency > 0) {
          return RECEIVE_ERROR;
        }

        on_websocket_receive(ctx);
        if (ctx->state == CONN_CLOSING)
          return RECEIVE_CLOSED;
        reset_websocket(ctx->ws);
        break;
      case PARSE_CONTINUE:
        signal = RECEIVE_CONTINUE;
        continue;
      }
    } else {
      switch (parse(ctx->payload, ctx->req)) {
      case PARSE_ERROR:
        return RECEIVE_ERROR;
      case PARSE_DONE:
        signal = RECEIVE_DONE;
        int inconsistency = handle_request_inconsistencies(req);
        if (inconsistency > 0) {
          switch (inconsistency) {
          case REQUEST_FAILURE_NON_NUMERIC_CONTENT_LENGTH:
          case REQUEST_FAILURE_NEGATIVE_CONTENT_LENGTH:
          case REQUEST_FAILURE_MISSING_HOST_HEADER:
          case REQUEST_FAILURE_MULTIPLE_HOST_HEADERS:
            send_status(ctx, STATUS_400);
            return RECEIVE_ERROR;
          default: {
          }
          }
        }
        on_request_finish(ctx);
        // there may be the start of a new request
        // already lined up in this buffer
        reset_request(req);
        break;
      case PARSE_CONTINUE:
        signal = RECEIVE_CONTINUE;
        continue;
      }
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

static void cleanup_socket(context *ctx) {
  close(ctx->socket);
  cleanup_context(ctx);
}

static void on_websocket_message(context *ctx) {
  size_t size;
  if (ctx->ws->header.opcode == WS_FRAME_PING) {
    ws_frame_header header = {.fin = 1,
                              .payload_len = 0,
                              .opcode = WS_FRAME_PONG,
                              .rsv1 = 0,
                              .rsv2 = 0,
                              .rsv3 = 0,
                              .has_mask = false};
    const unsigned char *out = serialize_websocket_bytes(&header, NULL, &size);
    send(ctx->socket, out, size, 0);
    return;
  } else if (ctx->ws->header.opcode == WS_FRAME_CLOSE) {
    ws_frame_header header = {.fin = 1,
                              .payload_len = 0,
                              .opcode = WS_FRAME_CLOSE,
                              .rsv1 = 0,
                              .rsv2 = 0,
                              .rsv3 = 0,
                              .has_mask = false};
    const unsigned char *out = serialize_websocket_bytes(&header, NULL, &size);
    send(ctx->socket, out, size, 0);
    ctx->state = CONN_CLOSING;
    return;
  }
  ws_frame_header header = {.fin = 1,
                            .payload_len = ctx->ws->header.payload_len,
                            .opcode = WS_FRAME_TEXT,
                            .rsv1 = 0,
                            .rsv2 = 0,
                            .rsv3 = 0,
                            .has_mask = false};
  const unsigned char *out =
      serialize_websocket_bytes(&header, ctx->ws->payload, &size);
  send(ctx->socket, out, size, 0);
}

static void on_request(context *ctx) {
  ssize_t out_len = 113;
  request_classification result = classify_request(ctx);
  switch (result.key) {
  case REQUEST_TYPE_WEBSOCKET_PAYLOAD: {
    break;
  }
  case REQUEST_TYPE_WEBSOCKET_UPGRADE: {
    response res = upgrade_websocket_response(result.websocket_key);
    size_t size;
    const char *out = serialize_response(&res, &size);
    send(ctx->socket, out, size, 0);
    // upgrading the user to WS
    ctx->state = CONN_WS;
    ctx->ws = new_websocket();
    free((char *)out);
    break;
  }
  default:
    printf("%u", result.key);
    char *out = "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html \r\n"
                "Content-Length: 47 \r\n\r\n"
                "<!DOCTYPE html>\n"
                "<body><div>hi lol</div></body>\n";
    send(ctx->socket, out, out_len, 0);
  }
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
  context *ctx = malloc(sizeof(context));
  *ctx = new_context(s, req);
  EV_SET(ev, s, EVFILT_READ, EV_ADD, 0, 0, ctx);
  if (kevent(kq, ev, 1, NULL, 0, NULL) < 0) {
    perror("kevent error");
  }
}

static void on_socket_receive(int socket, struct kevent *event) {
  context *ctx = (context *)event->udata;
  if ((event->flags & EV_EOF) && event->data == 0) {
    cleanup_socket(ctx);
    return;
  }
  switch (receive(ctx, on_request, on_websocket_message)) {
  case RECEIVE_CLOSED:
  case RECEIVE_ERROR:
    cleanup_socket(ctx);
    break;
  case RECEIVE_DONE:
    // HTTP/1.0 connections are not persistent
    if (ctx->req->version_number == HTTP_1_0) {
      cleanup_socket(ctx);
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

static pid_t workers[WORKER_COUNT];
static int worker_index = 0;

static void on_parent_sigint(int sig) {
  for (int i = 0; i < worker_index; i++)
    kill(workers[i], SIGTERM);
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

  signal(SIGINT, on_parent_sigint);
#ifdef DEBUG
  printf("Running in debug mode\n");
  run_worker(fd);
#else
  // extra workers don't seem to increase capacity by any amount?
  // size_t count_len = sizeof(count);
  // sysctlbyname("hw.perflevel0.logicalcpu", &count, &count_len, NULL, 0);
  for (size_t worker = 0; worker < WORKER_COUNT; worker++) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    }
    if (pid == 0) {
      run_worker(fd);
    }
    workers[worker_index++] = pid;
  }
  close(fd);
  while (wait(NULL) > 0)
    ;
#endif

  return 0;
}
