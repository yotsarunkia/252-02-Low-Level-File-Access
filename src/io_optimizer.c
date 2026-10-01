#include "io_optimizer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static long long now_us(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

int parse_block_size(const char *text, size_t *out_block_size) {
    char *endptr = NULL;
    long value;

    if (text == NULL || out_block_size == NULL) {
        return -1;
    }

    errno = 0;
    value = strtol(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != '\0') {
        return -1;
    }
    if (value < 1 || value > 4096) {
        return -1;
    }

    *out_block_size = (size_t)value;
    return 0;
}

int copy_with_metrics(const char *input_path, const char *output_path, size_t block_size, metrics_t *m) {
    int in_fd = -1;
    int out_fd = -1;
    char *buf = NULL;
    long long start_us;
    long long end_us;

    if (input_path == NULL || output_path == NULL || m == NULL || block_size == 0) {
        return -1;
    }

    m->bytes = 0;
    m->read_calls = 0;
    m->write_calls = 0;
    m->elapsed_us = 0;

    in_fd = open(input_path, O_RDONLY);
    if (in_fd < 0) {
        perror("open input");
        return -1;
    }

    out_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (out_fd < 0) {
        perror("open output");
        close(in_fd);
        return -1;
    }

    buf = (char *)malloc(block_size);
    if (buf == NULL) {
        perror("malloc");
        close(in_fd);
        close(out_fd);
        return -1;
    }

    start_us = now_us();
    if (start_us < 0) {
        perror("gettimeofday start");
        free(buf);
        close(in_fd);
        close(out_fd);
        return -1;
    }

    while (1) {
        ssize_t n = read(in_fd, buf, block_size);
        m->read_calls++;

        if (n < 0) {
            perror("read");
            free(buf);
            close(in_fd);
            close(out_fd);
            return -1;
        }
        if (n == 0) {
            break;
        }

        m->bytes += (long long)n;

        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out_fd, buf + written, (size_t)(n - written));
            m->write_calls++;
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("write");
                free(buf);
                close(in_fd);
                close(out_fd);
                return -1;
            }
            written += w;
        }
    }

    end_us = now_us();
    if (end_us < 0) {
        perror("gettimeofday end");
        free(buf);
        close(in_fd);
        close(out_fd);
        return -1;
    }
    m->elapsed_us = end_us - start_us;

    free(buf);
    if (close(in_fd) != 0) {
        perror("close input");
        close(out_fd);
        return -1;
    }
    if (close(out_fd) != 0) {
        perror("close output");
        return -1;
    }

    return 0;
}

void print_metrics(const metrics_t *m) {
    printf("bytes=%lld\n", m->bytes);
    printf("read_calls=%lld\n", m->read_calls);
    printf("write_calls=%lld\n", m->write_calls);
    printf("elapsed_us=%lld\n", m->elapsed_us);
}

int main(int argc, char **argv) {
    size_t block_size = 0;
    metrics_t m = {0, 0, 0, 0};

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <output_file> <block_size>\n", argv[0]);
        return 1;
    }

    if (parse_block_size(argv[3], &block_size) != 0) {
        fprintf(stderr, "block_size must be an integer in [1, 4096]\n");
        return 1;
    }

    if (copy_with_metrics(argv[1], argv[2], block_size, &m) != 0) {
        return 1;
    }

    print_metrics(&m);
    return 0;
}