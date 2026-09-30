#include <stddef.h>
#include <stdint.h>
#include <sys/_types/_ssize_t.h>
#define WEBSOCKET_HEADER_LENGTH 14

typedef enum {
  WS_FRAME_CONTINUATION = 0x0,
  WS_FRAME_TEXT = 0x1,
  WS_FRAME_BINARY = 0x2,
  WS_FRAME_RESERVED_NONCONTROL = 0x3,
  WS_FRAME_CLOSE = 0x8,
  WS_FRAME_PING = 0x9,
  WS_FRAME_PONG = 0xA,
  WS_FRAME_RESERVED_CONTROL = 0xB,
} websocket_frame_type;

typedef struct {
  unsigned char fin : 1;
  unsigned char rsv1 : 1;
  unsigned char rsv2 : 1;
  unsigned char rsv3 : 1;
  websocket_frame_type opcode;
  uint8_t mask[4];
  bool has_mask;
  uint64_t payload_len;
} ws_frame_header;

typedef struct {
  unsigned char buf[WEBSOCKET_HEADER_LENGTH];
  size_t i;
  ssize_t bytes_read;
  bool fully_received;
} ws_header_buffer;
