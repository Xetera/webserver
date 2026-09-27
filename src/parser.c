#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t count(const char *buf, char c) {
  size_t i = 0;
  while (buf[i] == c)
    i++;
  return i;
}

static char *append_slice(const char *old, const char *new, size_t n) {
  size_t len = (old == NULL ? 0 : strlen(old)) + n + 1;
  char *out = malloc(len);
  snprintf(out, len, "%s%s", old ?: "", new);
  return out;
}

static bool read_until(payload *p, char c, char **to_write) {
  const char *curr = &p->buf[p->i];
  const char *needle = strchr(curr, c);
  ptrdiff_t size = needle == NULL ? BUF_SIZE - p->i : needle - curr;
  char *new_string = append_slice(*to_write, curr, size);
  // could this be done more efficiently?
  free(*to_write);
  *to_write = new_string;
  if (needle == NULL) {
    p->i = BUF_SIZE;
    return false;
  } else {
    p->i += size + 1;
    return true;
  }
}

static bool expect_char(payload *p, char c) {
  if (p->buf[p->i] == c) {
    p->i++;
    return true;
  }
  return false;
}

static bool accept_tchar(payload *p) {
  char c = p->buf[p->i];
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '*':
  case '+':
  case '-':
  case '.':
  case '^':
  case '_':
  case '`':
  case '|':
  case '~':
    return true;
  default:
    return isalpha(c) || isdigit(c);
  }
}

/**
 * Moves `i` forward past whitespace in the buffer
 * and returns whether a non-whitespace character
 * in the buffer was reacherd
 */
static bool discard_whitespace(payload *p) {
  size_t distance = count(&p->buf[p->i], SP);
  p->i += distance;
  return distance == 0;
}

parse_signal parse_request_line(payload *p, request *req) {
  while (p->i < p->bytes_read) {
    const char *curr = &p->buf[p->i];
    switch (req->rl_state) {
    case RL_START: {
      if (discard_whitespace(p)) {
        req->rl_state = RL_METHOD;
      }
      break;
    }
    case RL_METHOD: {
      if (read_until(p, SP, &req->method)) {
        req->rl_state = RL_PATH_SPACING;
      }
      break;
    }
    case RL_PATH_SPACING: {
      if (discard_whitespace(p)) {
        req->rl_state = RL_PATH;
      }
      break;
    }
    case RL_PATH: {
      if (read_until(p, SP, &req->path)) {
        req->rl_state = RL_VERSION_SPACING;
      }
      break;
    }
    case RL_VERSION_SPACING: {
      if (discard_whitespace(p)) {
        req->rl_state = RL_VERSION;
      }
      break;
    }
    case RL_VERSION: {
      if (read_until(p, CR, &req->version)) {
        req->rl_state = RL_NEWLINE;
      }
      break;
    }
    case RL_NEWLINE: {
      if (*curr != '\n') {
        printf("expected newline after \r\n");
        return PARSE_ERROR;
      }
      p->i++;
      req->rl_state = RL_DONE;
    }
    case RL_DONE:
      if (strncmp(req->version, "HTTP/1.1", 8) == 0) {
        req->version_number = HTTP_1_1;
      }
      printf("method='%s' path='%s' version='%s'\n", req->method, req->path,
             req->version);
      return PARSE_DONE;
    }
  }
  return PARSE_CONTINUE;
}

parse_signal parse_headers(payload *p, request *req) {
  while (p->i < p->bytes_read) {
    switch (req->hl_state) {
    case HL_KEY: {
      header *header = req->expect_new_header ? new_header(&req->headers)
                                              : last_header(&req->headers);
      req->expect_new_header = false;
      if (read_until(p, ':', &header->key)) {
        req->hl_state = HL_VALUE_SPACING;
      }
      break;
    }
    case HL_VALUE_SPACING: {
      if (discard_whitespace(p)) {
        req->hl_state = HL_VALUE;
      }
      break;
    }
    case HL_VALUE: {
      header *header = last_header(&req->headers);
      if (read_until(p, CR, &header->value)) {
        req->hl_state = HL_LF;
      }
      break;
    }
    case HL_LF: {
      if (expect_char(p, '\n')) {
        req->hl_state = HL_LFCR;
        break;
      }
      printf("did not find an expected newline\n");
      return PARSE_ERROR;
    }
    case HL_LFCR: {
      if (expect_char(p, '\r')) {
        req->hl_state = HL_LFCRLF;
      } else if (accept_tchar(p)) {
        req->expect_new_header = true;
        req->hl_state = HL_KEY;
      } else {
        printf("Expected \\r\\n to either finish reading the headers or start "
               "another header but got (%c)",
               p->buf[p->i]);
        return PARSE_ERROR;
      }
      break;
    }
    case HL_LFCRLF: {
      if (expect_char(p, '\n')) {
        req->hl_state = HL_DONE;
      } else {
        printf("Expected \\r\\n\\r\\n to finish headers but got (%c) instead\n",
               p->buf[p->i]);
        return PARSE_ERROR;
      }
    }
    case HL_DONE:
      return PARSE_DONE;
    }
  }
  return PARSE_CONTINUE;
}

parse_signal parse_body(payload *p, request *req) {
  while (p->i < p->bytes_read) {
    switch (req->b_state) {
    case B_START:
    case B_READING: {
      int remaining = req->body_bytes_remaining - req->body_bytes_read;
      if (remaining <= 0) {
        return PARSE_DONE;
      }

      size_t copy_amount = p->bytes_read - p->i;
      memcpy(&req->body[req->body_bytes_read], &p->buf[p->i], copy_amount);
      req->body_bytes_read += copy_amount;
      break;
    }
    }
  }
  return PARSE_CONTINUE;
}

parse_signal parse(payload *p, request *req) {
  parse_signal signal;
  while (p->i < p->bytes_read) {
    switch (req->state) {
    case START:
      signal = parse_request_line(p, req);
      break;
    case PARSING_HEADERS:
      signal = parse_headers(p, req);
      const char *length = header_get(&req->headers, "content-length");
      if (length == NULL) {
        const char *encoding = header_get(&req->headers, "transfer-encoding");
        if (encoding == NULL) {
          return PARSE_DONE;
        }
        printf("Transfer encoding not implemented.\n");
        return PARSE_ERROR;
      }
      size_t byte_amount = atoi((char *)length);
      req->body_bytes_remaining = byte_amount;
      req->body = malloc(sizeof(char) * byte_amount);
      req->body[byte_amount - 1] = '\0';
      req->b_state = B_READING;
      break;
    case PARSING_BODY:
      signal = parse_body(p, req);
      break;
    }
    switch (signal) {
    case PARSE_CONTINUE:
      break;
    case PARSE_DONE:
      if (req->state == START) {
        req->state = PARSING_HEADERS;
      } else if (req->state == PARSING_HEADERS) {
        if (req->body_bytes_remaining == 0) {
          return PARSE_DONE;
        }
        req->state = PARSING_BODY;
      } else if (req->state == PARSING_BODY) {
        return PARSE_DONE;
      }
      break;
    case PARSE_ERROR:
      printf("something went wrong parsing\n");
      return PARSE_ERROR;
    }
  }
  return PARSE_CONTINUE;
}
