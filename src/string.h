#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// should this be inline? idk
static inline char *append_slice(const char *old, const char *new_str,
                                 size_t n) {
  size_t old_len = old == NULL ? 0 : strlen(old);
  size_t len = old_len + n + 1;
  char *out = (char *)malloc(len);
  if (old) {
    memcpy(out, old, old_len);
  }
  memcpy(out + old_len, new_str, n);
  out[old_len + n] = 0;
  return out;
}
