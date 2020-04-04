#ifndef UDP_ECHO
#define UDP_ECHO

struct ring_meta {
  uint64_t head;
  uint64_t tail;
  uint64_t len;
}

#endif
