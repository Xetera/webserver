#include "websocket.h"
#include "base64.h"
#include "sha1.h"
#include "string.h"
#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>

websocket *new_websocket() {
  websocket *ws = malloc(sizeof(websocket));
  ws->header = (ws_frame_header){
      .fin = 0,
      .rsv1 = 0,
      .rsv2 = 0,
      .rsv3 = 0,
      .payload_len = 0,
      .opcode = 0,
      .mask = {0},
  };
  ws->header_buffer =
      (ws_header_buffer){.bytes_read = 0, .i = 0, .fully_received = false};
  ws->payload = new_payload();
  return ws;
}

void reset_websocket(websocket *ws) {
  ws->header = (ws_frame_header){
      .fin = 0,
      .rsv1 = 0,
      .rsv2 = 0,
      .rsv3 = 0,
      .payload_len = 0,
      .opcode = 0,
      .mask = {0},
  };
  ws->header_buffer =
      (ws_header_buffer){.bytes_read = 0, .i = 0, .fully_received = false};
  free(ws->payload);
  ws->payload = new_payload();
}

response upgrade_websocket_response(const char *websocket_key) {
  response res = new_res();
  res.status = STATUS_101;
  res.headers = new_header_list();
  add_header(&res.headers, "Upgrade", "websocket");
  add_header(&res.headers, "Connection", "Upgrade");
  const char *accept = websocket_hash_key(websocket_key);
  add_header(&res.headers, "Sec-Websocket-Accept", accept);
  return res;
}

const char *websocket_hash_key(const char *websocket_key) {
  if (websocket_key == NULL || strlen(websocket_key) != 24) {
    perror("websocket_key invalid");
    return NULL;
  }
  char payload[WEBSOCKET_KEY_LENGTH + WEBSOCKET_MAGIC_GUID_LENGTH];
  memcpy(payload, websocket_key, WEBSOCKET_KEY_LENGTH);
  memcpy(payload + WEBSOCKET_KEY_LENGTH, WEBSOCKET_MAGIC_GUID,
         WEBSOCKET_MAGIC_GUID_LENGTH);

  size_t out_len = 0;
  unsigned char digest[SHA1_BYTE_LENGTH];
  SHA1((char *)digest, (const char *)payload,
       WEBSOCKET_KEY_LENGTH + WEBSOCKET_MAGIC_GUID_LENGTH);
  unsigned char *encoded = base64_encode(digest, sizeof(digest), &out_len);
  if (encoded == NULL) {
    return NULL;
  }
  encoded[out_len] = 0;
  return (const char *)encoded;
}

static size_t ws_header_size(const unsigned char *buffer) {
  uint8_t declared_size = buffer[WEBSOCKET_PAYLOAD_LEN_OFFSET] & 0b01111111;
  bool has_mask = buffer[WEBSOCKET_PAYLOAD_LEN_OFFSET] & 0b10000000;
  size_t size = 2;
  if (declared_size == 127) {
    size += 8;
  } else if (declared_size == 126) {
    size += 2;
  }
  if (has_mask)
    size += 4;

  return size;
}

static websocket_frame_type parse_opcode(uint8_t opcode) {
  switch (opcode) {
  case 0x0:
    return WS_FRAME_CONTINUATION;
  case 0x1:
    return WS_FRAME_TEXT;
  case 0x2:
    return WS_FRAME_BINARY;
  case 0x3:
  case 0x4:
  case 0x5:
  case 0x6:
  case 0x7:
    return WS_FRAME_RESERVED_NONCONTROL;
  case 0x8:
    return WS_FRAME_CLOSE;
  case 0x9:
    return WS_FRAME_PING;
  case 0xA:
    return WS_FRAME_PONG;
  default:
    // shoudln't happen?
    return WS_FRAME_RESERVED_CONTROL;
  }
}

static websocket_frame_type serialize_opcode(websocket_frame_type opcode) {
  switch (opcode) {
  case WS_FRAME_CONTINUATION:
    return 0x0;
  case WS_FRAME_TEXT:
    return 0x1;
  case WS_FRAME_BINARY:
    return 0x2;
    // oo not good..
  case WS_FRAME_RESERVED_NONCONTROL:
    return 0x3;
  case WS_FRAME_CLOSE:
    return 0x8;
  case WS_FRAME_PING:
    return 0x9;
  case WS_FRAME_PONG:
    return 0xA;
  default:
    // shoudln't happen?
    return 0xB;
  }
}

static void populate_header(ws_header_buffer *buffer, ws_frame_header *header,
                            size_t header_size) {
  uint8_t declared_size =
      buffer->buf[WEBSOCKET_PAYLOAD_LEN_OFFSET] & 0b01111111;
  header->fin = (buffer->buf[0] >> 7) & 1;
  header->rsv1 = (buffer->buf[0] >> 6) & 1;
  header->rsv2 = (buffer->buf[0] >> 5) & 1;
  header->rsv3 = (buffer->buf[0] >> 4) & 1;
  header->opcode = parse_opcode(buffer->buf[0] & 0x0F);
  bool has_mask = buffer->buf[1] >> 7;
  header->has_mask = has_mask;
  if (!has_mask && header_size == 4) {
    uint16_t len16;
    memcpy(&len16, &buffer->buf[2], 2);
    header->payload_len = ntohs(len16);
  } else if (!has_mask && header_size == 10) {
    uint64_t len64;
    memcpy(&len64, &buffer->buf[2], 8);
    header->payload_len = ntohll(len64);
  } else if (!has_mask && header_size == 2) {
    header->payload_len = declared_size;
  } else if (has_mask && header_size == 6) {
    header->payload_len = declared_size;
    memcpy(&header->mask, &buffer->buf[2], 4);
  } else if (has_mask && header_size == 8) {
    uint16_t len16;
    memcpy(&len16, &buffer->buf[2], 2);
    header->payload_len = ntohs(len16);
    memcpy(&header->mask, &buffer->buf[4], 4);
  } else if (has_mask && header_size == 14) {
    uint64_t len64;
    memcpy(&len64, &buffer->buf[2], 8);
    header->payload_len = ntohll(len64);
    memcpy(&header->mask, &buffer->buf[10], 4);
  }
}

static void mask_body(unsigned char *buf, size_t body_length,
                      ws_frame_header *header) {
  for (size_t i = 0; i < body_length; i++) {
    buf[i] ^= header->mask[i % 4];
  }
}

parse_signal parse_websocket(payload *p, websocket *ws) {
  if (!ws->header_buffer.fully_received) {
    size_t size = p->bytes_read < 2
                      ? 2
                      : ws_header_size((const unsigned char *)&p->buf[p->i]);
    memcpy(ws->header_buffer.buf + ws->header_buffer.i, &p->buf[p->i], size);
    ws->header_buffer.i += size;
    p->i += size;
    if (ws->header_buffer.i < size) {
      return PARSE_CONTINUE;
    }
    populate_header(&ws->header_buffer, &ws->header, size);
    ws->header_buffer.fully_received = true;
  }

  size_t body_copy = p->bytes_read - p->i;
  if (body_copy > ws->header.payload_len) {
    body_copy = ws->header.payload_len;
  }

  memcpy(&ws->payload->buf[ws->payload->i], &p->buf[p->i], body_copy);
  p->i += body_copy;
  ws->payload->i += body_copy;

  if (ws->payload->i < ws->header.payload_len) {
    return PARSE_CONTINUE;
  }
  mask_body((unsigned char *)ws->payload->buf, ws->payload->i, &ws->header);
  return PARSE_DONE;
}

const unsigned char *serialize_websocket_bytes(ws_frame_header *header,
                                               const char *payload,
                                               size_t *out_len) {
  size_t header_len = 2;
  if (header->has_mask) {
    header_len += 4;
  }
  if (header->payload_len > 65535) {
    header_len += 8;
  } else if (header->payload_len > 128) {
    header_len += 2;
  }

  *out_len = header_len + header->payload_len;
  unsigned char *buffer = (unsigned char *)malloc(*out_len);
  buffer[0] = (1 << 7) | serialize_opcode(header->opcode);
  buffer[1] = (header->has_mask ? (1 << 7) : 0);

  size_t offset = 0;
  if (header->payload_len > 65535) {
    buffer[1] |= 127;
    uint64_t len64 = htonll(header->payload_len);
    memcpy(&buffer[2], &len64, 8);
    offset += 8;
  } else if (header->payload_len > 128) {
    buffer[1] |= 126;
    uint16_t len16 = htons(header->payload_len);
    memcpy(&buffer[2], &len16, 2);
    offset += 2;
  } else {
    uint8_t len8 = (uint8_t)header->payload_len;
    buffer[1] |= len8;
  }
  if (header->has_mask) {
    memcpy(&buffer[2 + offset], header->mask, 4);
    offset += 4;
  }

  memcpy(&buffer[2 + offset], payload, header->payload_len);
  if (header->has_mask) {
    mask_body(&buffer[2 + offset], header->payload_len, header);
  }
  return buffer;
}
