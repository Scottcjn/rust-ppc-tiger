/*
 * wget for PowerPC Mac OS X Tiger
 * Uses mbedTLS for HTTPS support
 *
 * Build:
 *   gcc -arch ppc -std=c99 -O2 -mcpu=7450 -DHAVE_MBEDTLS \
 *       -I./mbedtls-2.28.8/include -o wget \
 *       wget_tiger.c pocketfox_ssl_tiger.c \
 *       -L./mbedtls-2.28.8/library -lmbedtls -lmbedx509 -lmbedcrypto
 *
 * Usage:
 *   ./wget https://example.com/file.txt
 *   ./wget -O output.txt https://example.com/file.txt
 *   ./wget -q https://example.com/file.txt  (quiet mode)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* Global for redirect handling */
char g_redirect_url[2048] = {0};
#define MAX_REDIRECTS 5

/* Forward declarations from pocketfox_ssl_tiger.c */
typedef struct PocketFoxSSL PocketFoxSSL;
extern int pocketfox_ssl_init(void);
extern void pocketfox_ssl_shutdown(void);
extern PocketFoxSSL* pocketfox_ssl_new(void);
extern void pocketfox_ssl_free(PocketFoxSSL* ctx);
extern int pocketfox_ssl_connect(PocketFoxSSL* ctx, const char* hostname, int port);
extern int pocketfox_ssl_read(PocketFoxSSL* ctx, unsigned char* buf, size_t len);
extern int pocketfox_ssl_write(PocketFoxSSL* ctx, const unsigned char* buf, size_t len);
extern void pocketfox_ssl_close(PocketFoxSSL* ctx);
extern const char* pocketfox_ssl_error(PocketFoxSSL* ctx);

/* ============================================
 * URL Parser
 * ============================================ */

typedef struct {
    char scheme[16];
    char host[256];
    int port;
    char path[2048];
    char filename[256];
} ParsedURL;

static int parse_url(const char* url, ParsedURL* out) {
    memset(out, 0, sizeof(ParsedURL));
    strcpy(out->path, "/");
    out->port = 443;
    strcpy(out->scheme, "https");

    const char* p = url;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        out->port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        strcpy(out->scheme, "http");
        out->port = 80;
        p += 7;
    }

    /* Extract host */
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) {
        out->host[i++] = *p++;
    }
    out->host[i] = '\0';

    /* Port? */
    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p && *p != '/') p++;
    }

    /* Path */
    if (*p == '/') {
        strncpy(out->path, p, sizeof(out->path) - 1);
    }

    /* Extract filename from path */
    const char* last_slash = strrchr(out->path, '/');
    if (last_slash && *(last_slash + 1)) {
        strncpy(out->filename, last_slash + 1, sizeof(out->filename) - 1);
        /* Remove query string */
        char* q = strchr(out->filename, '?');
        if (q) *q = '\0';
    }
    if (out->filename[0] == '\0') {
        strcpy(out->filename, "index.html");
    }

    return strlen(out->host) > 0;
}

/* ============================================
 * HTTP Download
 * ============================================ */

static int download_https(const char* host, int port, const char* path,
                          FILE* output, int quiet, long* content_length) {
    PocketFoxSSL* ssl = pocketfox_ssl_new();
    if (!ssl) {
        fprintf(stderr, "wget: Failed to create SSL context\n");
        return -1;
    }

    if (pocketfox_ssl_connect(ssl, host, port) != 0) {
        fprintf(stderr, "wget: %s\n", pocketfox_ssl_error(ssl));
        pocketfox_ssl_free(ssl);
        return -1;
    }

    if (!quiet) {
        fprintf(stderr, "Connecting to %s:%d... connected.\n", host, port);
    }

    /* Send HTTP request */
    char request[4096];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Wget/1.0 (PowerPC Tiger; mbedTLS)\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    pocketfox_ssl_write(ssl, (const unsigned char*)request, strlen(request));

    if (!quiet) {
        fprintf(stderr, "HTTP request sent, awaiting response... ");
    }

    /* Read response headers */
    char header_buf[8192];
    int header_len = 0;
    int status_code = 0;
    *content_length = -1;

    while (header_len < sizeof(header_buf) - 1) {
        int n = pocketfox_ssl_read(ssl, (unsigned char*)header_buf + header_len, 1);
        if (n <= 0) break;
        header_len++;

        /* Check for end of headers */
        if (header_len >= 4 &&
            header_buf[header_len-4] == '\r' && header_buf[header_len-3] == '\n' &&
            header_buf[header_len-2] == '\r' && header_buf[header_len-1] == '\n') {
            break;
        }
    }
    header_buf[header_len] = '\0';

    /* Parse status code */
    if (strncmp(header_buf, "HTTP/1.", 7) == 0) {
        status_code = atoi(header_buf + 9);
    }

    if (!quiet) {
        fprintf(stderr, "%d\n", status_code);
    }

    /* Check for redirect */
    if (status_code >= 300 && status_code < 400) {
        char* loc = strstr(header_buf, "Location: ");
        if (loc) {
            loc += 10;
            char* end = strstr(loc, "\r\n");
            if (end) *end = '\0';
            if (!quiet) {
                fprintf(stderr, "Redirecting to %s\n", loc);
            }
            /* Store redirect URL for caller */
            extern char g_redirect_url[2048];
            strncpy(g_redirect_url, loc, sizeof(g_redirect_url) - 1);
        }
        pocketfox_ssl_close(ssl);
        pocketfox_ssl_free(ssl);
        return -2;  /* Redirect */
    }

    if (status_code != 200) {
        fprintf(stderr, "wget: Server returned %d\n", status_code);
        pocketfox_ssl_close(ssl);
        pocketfox_ssl_free(ssl);
        return -1;
    }

    /* Parse Content-Length */
    char* cl = strstr(header_buf, "Content-Length: ");
    if (cl) {
        *content_length = atol(cl + 16);
    }

    /* Check for chunked transfer encoding */
    int chunked = 0;
    if (strstr(header_buf, "Transfer-Encoding: chunked") != NULL ||
        strstr(header_buf, "transfer-encoding: chunked") != NULL) {
        chunked = 1;
        if (!quiet) {
            fprintf(stderr, "Length: unspecified [chunked]\n");
        }
    } else if (!quiet && *content_length > 0) {
        fprintf(stderr, "Length: %ld", *content_length);
        if (*content_length > 1024*1024) {
            fprintf(stderr, " (%.1fM)", *content_length / (1024.0*1024.0));
        } else if (*content_length > 1024) {
            fprintf(stderr, " (%.1fK)", *content_length / 1024.0);
        }
        fprintf(stderr, "\n");
    }

    /* Download body */
    unsigned char buf[8192];
    long total = 0;
    time_t start_time = time(NULL);
    time_t last_progress = start_time;

    if (chunked) {
        /* Handle chunked transfer encoding */
        while (1) {
            /* Read chunk size line */
            char size_line[32];
            int si = 0;
            while (si < 31) {
                int n = pocketfox_ssl_read(ssl, (unsigned char*)&size_line[si], 1);
                if (n <= 0) break;
                if (size_line[si] == '\n') {
                    size_line[si] = '\0';
                    break;
                }
                si++;
            }
            size_line[si] = '\0';

            /* Parse chunk size (hex) */
            long chunk_size = strtol(size_line, NULL, 16);
            if (chunk_size <= 0) break;  /* End of chunks */

            /* Read chunk data */
            long chunk_read = 0;
            while (chunk_read < chunk_size) {
                int to_read = (chunk_size - chunk_read > sizeof(buf)) ? sizeof(buf) : (chunk_size - chunk_read);
                int n = pocketfox_ssl_read(ssl, buf, to_read);
                if (n <= 0) break;
                fwrite(buf, 1, n, output);
                chunk_read += n;
                total += n;

                /* Progress */
                if (!quiet) {
                    time_t now = time(NULL);
                    if (now != last_progress) {
                        last_progress = now;
                        fprintf(stderr, "\r%ld bytes received", total);
                        fflush(stderr);
                    }
                }
            }

            /* Read trailing CRLF after chunk */
            char crlf[2];
            pocketfox_ssl_read(ssl, (unsigned char*)crlf, 2);
        }
    } else {
        /* Regular (non-chunked) download */
        while (1) {
            int n = pocketfox_ssl_read(ssl, buf, sizeof(buf));
            if (n <= 0) break;

            fwrite(buf, 1, n, output);
            total += n;

            /* Progress indicator */
            if (!quiet) {
                time_t now = time(NULL);
                if (now != last_progress) {
                    last_progress = now;
                    if (*content_length > 0) {
                        int pct = (int)(100.0 * total / *content_length);
                        fprintf(stderr, "\r%3d%% [", pct);
                        int bars = pct / 5;
                        int i;
                        for (i = 0; i < 20; i++) {
                            fprintf(stderr, i < bars ? "=" : " ");
                        }
                        fprintf(stderr, "] %ld/%ld", total, *content_length);
                    } else {
                        fprintf(stderr, "\r%ld bytes received", total);
                    }
                    fflush(stderr);
                }
            }
        }
    }

    if (!quiet) {
        time_t elapsed = time(NULL) - start_time;
        if (elapsed == 0) elapsed = 1;
        fprintf(stderr, "\n\n%ld bytes in %lds (%.1f KB/s)\n",
                total, elapsed, total / 1024.0 / elapsed);
    }

    pocketfox_ssl_close(ssl);
    pocketfox_ssl_free(ssl);

    return 0;
}

/* ============================================
 * Plain HTTP (port 80)
 * ============================================ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

static int download_http(const char* host, int port, const char* path,
                         FILE* output, int quiet, long* content_length) {
    struct hostent* he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "wget: Cannot resolve %s\n", host);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("wget: socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("wget: connect");
        close(sock);
        return -1;
    }

    if (!quiet) {
        fprintf(stderr, "Connecting to %s:%d... connected.\n", host, port);
    }

    /* Send request */
    char request[4096];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Wget/1.0 (PowerPC Tiger)\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    write(sock, request, strlen(request));

    if (!quiet) {
        fprintf(stderr, "HTTP request sent, awaiting response... ");
    }

    /* Read headers */
    char header_buf[8192];
    int header_len = 0;
    int status_code = 0;
    *content_length = -1;

    while (header_len < sizeof(header_buf) - 1) {
        int n = read(sock, header_buf + header_len, 1);
        if (n <= 0) break;
        header_len++;

        if (header_len >= 4 &&
            header_buf[header_len-4] == '\r' && header_buf[header_len-3] == '\n' &&
            header_buf[header_len-2] == '\r' && header_buf[header_len-1] == '\n') {
            break;
        }
    }
    header_buf[header_len] = '\0';

    if (strncmp(header_buf, "HTTP/1.", 7) == 0) {
        status_code = atoi(header_buf + 9);
    }

    if (!quiet) {
        fprintf(stderr, "%d\n", status_code);
    }

    if (status_code != 200) {
        fprintf(stderr, "wget: Server returned %d\n", status_code);
        close(sock);
        return -1;
    }

    char* cl = strstr(header_buf, "Content-Length: ");
    if (cl) {
        *content_length = atol(cl + 16);
    }

    /* Download */
    unsigned char buf[8192];
    long total = 0;

    while (1) {
        int n = read(sock, buf, sizeof(buf));
        if (n <= 0) break;
        fwrite(buf, 1, n, output);
        total += n;

        if (!quiet) {
            fprintf(stderr, "\r%ld bytes received", total);
            fflush(stderr);
        }
    }

    if (!quiet) {
        fprintf(stderr, "\n");
    }

    close(sock);
    return 0;
}

/* ============================================
 * Main
 * ============================================ */

static void usage(void) {
    fprintf(stderr, "Usage: wget [OPTIONS] URL\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -O FILE    Save to FILE\n");
    fprintf(stderr, "  -q         Quiet mode\n");
    fprintf(stderr, "  -h         Show help\n");
    fprintf(stderr, "  --version  Show version\n");
    fprintf(stderr, "\nBuilt with mbedTLS for HTTPS on PowerPC Tiger\n");
}

int main(int argc, char** argv) {
    char* output_file = NULL;
    char* url = NULL;
    int quiet = 0;
    int i;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("wget 1.0 (PowerPC Tiger, mbedTLS)\n");
            return 0;
        } else if (argv[i][0] != '-') {
            url = argv[i];
        }
    }

    if (!url) {
        usage();
        return 1;
    }

    /* Parse URL */
    ParsedURL parsed;
    if (!parse_url(url, &parsed)) {
        fprintf(stderr, "wget: Invalid URL: %s\n", url);
        return 1;
    }

    /* Determine output file */
    if (!output_file) {
        output_file = parsed.filename;
    }

    /* Open output */
    FILE* output;
    if (strcmp(output_file, "-") == 0) {
        output = stdout;
        quiet = 1;
    } else {
        output = fopen(output_file, "wb");
        if (!output) {
            fprintf(stderr, "wget: Cannot create %s: %s\n", output_file, strerror(errno));
            return 1;
        }
        if (!quiet) {
            fprintf(stderr, "Saving to: '%s'\n\n", output_file);
        }
    }

    /* Initialize SSL */
    pocketfox_ssl_init();

    /* Download with redirect following */
    long content_length = 0;
    int ret;
    int redirects = 0;

    do {
        g_redirect_url[0] = '\0';  /* Clear redirect URL */

        if (strcmp(parsed.scheme, "https") == 0) {
            ret = download_https(parsed.host, parsed.port, parsed.path,
                                output, quiet, &content_length);
        } else {
            ret = download_http(parsed.host, parsed.port, parsed.path,
                               output, quiet, &content_length);
        }

        /* Handle redirect */
        if (ret == -2 && g_redirect_url[0] != '\0' && redirects < MAX_REDIRECTS) {
            redirects++;
            if (!parse_url(g_redirect_url, &parsed)) {
                fprintf(stderr, "wget: Invalid redirect URL: %s\n", g_redirect_url);
                ret = -1;
                break;
            }
            /* Reopen output file if needed (truncate) */
            if (output != stdout) {
                fseek(output, 0, SEEK_SET);
                ftruncate(fileno(output), 0);
            }
        }
    } while (ret == -2 && redirects < MAX_REDIRECTS);

    if (ret == -2) {
        fprintf(stderr, "wget: Too many redirects\n");
        ret = -1;
    }

    if (output != stdout) {
        fclose(output);
    }

    pocketfox_ssl_shutdown();

    if (ret != 0 && output != stdout) {
        unlink(output_file);  /* Remove partial file */
    }

    return ret == 0 ? 0 : 1;
}
