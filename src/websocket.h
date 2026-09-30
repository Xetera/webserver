#pragma once

#include "parse_state.h"
#include "payload.h"
#include "response.h"
#include "websocket_header.h"
#include <stddef.h>

#define WEBSOCKET_SUPPORTED_VERSION 13
#define WEBSOCKET_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WEBSOCKET_MAGIC_GUID_LENGTH 36
#define WEBSOCKET_KEY_LENGTH 24

#define WEBSOCKET_PAYLOAD_LEN_OFFSET 1

typedef enum { WS_CONNECTING, WS_CONNECTED } websocket_connection_state;

#define IS_WS_CONTROL_FRAME(x) ((x) & 0x8) != 0

typedef struct websocket {
  ws_header_buffer header_buffer;
  ws_frame_header header;
  payload *payload;
} websocket;

websocket *new_websocket();
void reset_websocket(websocket *);
const char *websocket_hash_key(const char *websocket_key);
response upgrade_websocket_response(const char *websocket_key);
parse_signal parse_websocket(payload *p, websocket *ws);

const unsigned char *serialize_websocket_bytes(ws_frame_header *header,
                                               const char *payload,
                                               size_t *out_len);
