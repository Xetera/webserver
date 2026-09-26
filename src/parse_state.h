#pragma once

#define BUF_SIZE 8
typedef enum { START, PARSING_HEADERS, PARSING_BODY } parse_state;
typedef enum { PARSE_CONTINUE, PARSE_ERROR, PARSE_DONE } parse_signal;

typedef enum {
  HL_KEY = 0,
  HL_VALUE_SPACING,
  HL_VALUE,
  HL_LF,
  HL_LFCR,
  HL_LFCRLF,
  HL_DONE,
} header_line_parse_state;

typedef enum {
  RL_START = 0,
  RL_METHOD,
  RL_PATH_SPACING,
  RL_PATH,
  RL_VERSION_SPACING,
  RL_VERSION,
  RL_NEWLINE,
  RL_DONE
} request_line_parse_state;
