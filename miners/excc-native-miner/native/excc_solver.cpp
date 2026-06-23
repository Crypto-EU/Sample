// Native EXCC Equihash 144/5 solver frontend.
//
// This frontend drives John Tromp's MIT-licensed Equihash solver with EXCC's
// 192-byte header layout and nonce location at byte offset 140.

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <vector>

// Silence progress printf calls embedded in equi_miner.h. This keeps the solver
// output machine-readable for the Python stratum process.
#define printf(...) ((void)0)
#include "equi_miner.h"
#undef printf

#define COMPRESSED_SOL_SIZE (PROOFSIZE * (DIGITBITS + 1) / 8)
#define EXCC_NONCE_OFFSET 140

static int hex_to_byte(const char *x) {
  int b = 0;
  for (int i = 0; i < 2; i++) {
    unsigned char c = (unsigned char)tolower(x[i]);
    if (!isxdigit(c)) {
      return -1;
    }
    b = (b << 4) | (c - (c >= '0' && c <= '9' ? '0' : ('a' - 10)));
  }
  return b;
}

static bool decode_hex(const char *hex, std::vector<unsigned char> &out) {
  const size_t hex_len = strlen(hex);
  if (hex_len == 0 || (hex_len & 1)) {
    return false;
  }

  out.resize(hex_len / 2);
  for (size_t i = 0; i < out.size(); i++) {
    const int b = hex_to_byte(hex + 2 * i);
    if (b < 0) {
      return false;
    }
    out[i] = (unsigned char)b;
  }

  return true;
}

static void compress_solution(const proof sol, unsigned char *csol) {
  unsigned char b = 0;
  for (uint32_t i = 0, j = 0, bits_left = DIGITBITS + 1; j < COMPRESSED_SOL_SIZE; csol[j++] = b) {
    if (bits_left >= 8) {
      b = sol[i] >> (bits_left -= 8);
    } else {
      b = sol[i];
      b <<= (8 - bits_left);
      bits_left += DIGITBITS + 1 - 8;
      b |= sol[++i] >> bits_left;
    }
  }
}

static double now_seconds() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void write_le32(std::vector<unsigned char> &data, size_t offset, uint32_t value) {
  data[offset + 0] = (unsigned char)(value & 0xff);
  data[offset + 1] = (unsigned char)((value >> 8) & 0xff);
  data[offset + 2] = (unsigned char)((value >> 16) & 0xff);
  data[offset + 3] = (unsigned char)((value >> 24) & 0xff);
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s -x HEX_HEADER -n NONCE -r RANGE -t THREADS [-p PERSONAL]\n"
          "Prints lines: solution <nonce-hex-little-endian> <compressed-solution-hex>\n",
          argv0);
}

int main(int argc, char **argv) {
  const char *hex_header = NULL;
  const char *personal = "ZcashPoW";
  uint32_t nonce = 0;
  uint32_t range = 1;
  int nthreads = 1;
  int c;

  while ((c = getopt(argc, argv, "x:n:r:t:p:")) != -1) {
    switch (c) {
      case 'x':
        hex_header = optarg;
        break;
      case 'n':
        nonce = (uint32_t)strtoul(optarg, NULL, 0);
        break;
      case 'r':
        range = (uint32_t)strtoul(optarg, NULL, 0);
        break;
      case 't':
        nthreads = atoi(optarg);
        break;
      case 'p':
        personal = optarg;
        break;
      default:
        usage(argv[0]);
        return 2;
    }
  }

  if (!hex_header || nthreads < 1 || range < 1) {
    usage(argv[0]);
    return 2;
  }

  std::vector<unsigned char> header;
  if (!decode_hex(hex_header, header)) {
    fprintf(stderr, "Invalid hex header.\n");
    return 2;
  }
  if (header.size() <= EXCC_NONCE_OFFSET + 4) {
    fprintf(stderr, "EXCC header must include nonce bytes at offset %u. Got %zu bytes.\n",
            EXCC_NONCE_OFFSET, header.size());
    return 2;
  }

  thread_ctx *threads = (thread_ctx *)calloc((size_t)nthreads, sizeof(thread_ctx));
  if (!threads) {
    fprintf(stderr, "Could not allocate thread contexts.\n");
    return 1;
  }

  equi eq((uint32_t)nthreads);
  uint32_t total_solutions = 0;
  const double started = now_seconds();

  for (uint32_t r = 0; r < range; r++) {
    const uint32_t current_nonce = nonce + r;
    write_le32(header, EXCC_NONCE_OFFSET, current_nonce);

    eq.setheadernonce((const char *)header.data(), (uint32_t)header.size(), personal);

    for (int t = 0; t < nthreads; t++) {
      threads[t].id = (uint32_t)t;
      threads[t].eq = &eq;
      const int err = pthread_create(&threads[t].thread, NULL, worker, (void *)&threads[t]);
      if (err != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(err));
        free(threads);
        return 1;
      }
    }

    for (int t = 0; t < nthreads; t++) {
      const int err = pthread_join(threads[t].thread, NULL);
      if (err != 0) {
        fprintf(stderr, "pthread_join failed: %s\n", strerror(err));
        free(threads);
        return 1;
      }
    }

    const uint32_t maxsols = min(MAXSOLS, eq.nsols);
    for (uint32_t s = 0; s < maxsols; s++) {
      if (duped(eq.sols[s])) {
        continue;
      }

      unsigned char csol[COMPRESSED_SOL_SIZE];
      compress_solution(eq.sols[s], csol);

      printf("solution %02x%02x%02x%02x ",
             (unsigned)(current_nonce & 0xff),
             (unsigned)((current_nonce >> 8) & 0xff),
             (unsigned)((current_nonce >> 16) & 0xff),
             (unsigned)((current_nonce >> 24) & 0xff));
      for (uint32_t i = 0; i < COMPRESSED_SOL_SIZE; i++) {
        printf("%02x", csol[i]);
      }
      printf("\n");
      total_solutions++;
    }
  }

  const double elapsed = now_seconds() - started;
  fprintf(stderr, "native-solver searched %u nonce(s), found %u solution(s), %.6f sol/s\n",
          range, total_solutions, elapsed > 0.0 ? (double)range / elapsed : 0.0);

  free(threads);
  return 0;
}
