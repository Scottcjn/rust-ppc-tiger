/*
 * git-remote-https for PowerPC Mac OS X Tiger
 * Wraps git remote operations with mbedTLS for TLS 1.2 support
 *
 * This allows Tiger's git to push/pull from GitHub!
 *
 * Build:
 *   gcc -arch ppc -std=c99 -O2 -DHAVE_MBEDTLS \
 *       -I./mbedtls-2.28.8/include -o git-remote-https \
 *       git_https_tiger.c pocketfox_ssl_tiger.c \
 *       -L./mbedtls-2.28.8/library -lmbedtls -lmbedx509 -lmbedcrypto
 *
 * Install:
 *   sudo cp git-remote-https /usr/local/libexec/git-core/
 *
 * Usage:
 *   git clone https://github.com/user/repo.git
 *   git push origin main
 *   (Works transparently!)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

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
    char host[256];
    int port;
    char path[2048];
} GitURL;

static int parse_git_url(const char* url, GitURL* out) {
    memset(out, 0, sizeof(GitURL));
    out->port = 443;

    const char* p = url;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else {
        fprintf(stderr, "git-remote-https: Only HTTPS URLs supported\n");
        return 0;
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
    } else {
        strcpy(out->path, "/");
    }

    return strlen(out->host) > 0;
}

/* ============================================
 * Git Smart HTTP Protocol
 * ============================================ */

static PocketFoxSSL* git_ssl = NULL;

static int git_https_connect(const char* host, int port) {
    pocketfox_ssl_init();

    git_ssl = pocketfox_ssl_new();
    if (!git_ssl) {
        fprintf(stderr, "git-remote-https: Failed to create SSL context\n");
        return -1;
    }

    if (pocketfox_ssl_connect(git_ssl, host, port) != 0) {
        fprintf(stderr, "git-remote-https: %s\n", pocketfox_ssl_error(git_ssl));
        pocketfox_ssl_free(git_ssl);
        git_ssl = NULL;
        return -1;
    }

    return 0;
}

static void git_https_disconnect(void) {
    if (git_ssl) {
        pocketfox_ssl_close(git_ssl);
        pocketfox_ssl_free(git_ssl);
        git_ssl = NULL;
    }
    pocketfox_ssl_shutdown();
}

/* Send HTTP request and get response */
static int git_https_request(const char* host, const char* path,
                             const char* method, const char* content_type,
                             const unsigned char* body, size_t body_len,
                             unsigned char* response, size_t* response_len) {
    char request[4096];
    int req_len;

    if (body && body_len > 0) {
        req_len = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: git/1.0 (PowerPC Tiger; mbedTLS)\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Accept: application/x-git-upload-pack-result, application/x-git-receive-pack-result\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            method, path, host, content_type, body_len);
    } else {
        req_len = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: git/1.0 (PowerPC Tiger; mbedTLS)\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            method, path, host);
    }

    /* Send request headers */
    if (pocketfox_ssl_write(git_ssl, (const unsigned char*)request, req_len) < 0) {
        return -1;
    }

    /* Send body if present */
    if (body && body_len > 0) {
        if (pocketfox_ssl_write(git_ssl, body, body_len) < 0) {
            return -1;
        }
    }

    /* Read response */
    size_t total = 0;
    int n;
    while ((n = pocketfox_ssl_read(git_ssl, response + total, *response_len - total)) > 0) {
        total += n;

        /* Check for end of chunked response or content-length */
        if (total > 4 && memcmp(response + total - 5, "0\r\n\r\n", 5) == 0) {
            break;
        }
    }

    *response_len = total;
    return 0;
}

/* ============================================
 * Git Remote Helper Protocol
 * ============================================ */

static void handle_capabilities(void) {
    printf("fetch\n");
    printf("push\n");
    printf("option\n");
    printf("\n");
    fflush(stdout);
}

static void handle_list(GitURL* url, int for_push) {
    char path[2048];
    snprintf(path, sizeof(path), "%s/info/refs?service=%s",
             url->path, for_push ? "git-receive-pack" : "git-upload-pack");

    if (git_https_connect(url->host, url->port) < 0) {
        fprintf(stderr, "git-remote-https: Connection failed\n");
        return;
    }

    unsigned char response[65536];
    size_t response_len = sizeof(response);

    if (git_https_request(url->host, path, "GET", NULL, NULL, 0,
                          response, &response_len) < 0) {
        fprintf(stderr, "git-remote-https: Request failed\n");
        git_https_disconnect();
        return;
    }

    /* Parse refs from response */
    /* Skip HTTP headers */
    char* body = strstr((char*)response, "\r\n\r\n");
    if (body) {
        body += 4;

        /* Parse git smart protocol response */
        /* Format: 4-digit hex length + data */
        char* p = body;
        while (*p) {
            /* Skip pkt-line header */
            if (p[0] == '0' && p[1] == '0' && p[2] == '0' && p[3] == '0') {
                break;  /* Flush packet */
            }

            char len_str[5] = {p[0], p[1], p[2], p[3], 0};
            int pkt_len = (int)strtol(len_str, NULL, 16);
            if (pkt_len <= 4) break;

            p += 4;  /* Skip length */

            /* Parse ref line: SHA1 SP ref NUL capabilities LF */
            char sha1[41];
            char ref[256];

            if (sscanf(p, "%40s %255s", sha1, ref) == 2) {
                /* Output in git remote helper format */
                printf("%s %s\n", sha1, ref);
            }

            p += pkt_len - 4;
        }
    }

    printf("\n");
    fflush(stdout);

    git_https_disconnect();
}

static void handle_fetch(GitURL* url, const char* sha1, const char* ref) {
    /* TODO: Implement git-upload-pack for fetch */
    fprintf(stderr, "git-remote-https: fetch not yet implemented\n");
    printf("\n");
    fflush(stdout);
}

static void handle_push(GitURL* url, const char* src, const char* dst) {
    /* TODO: Implement git-receive-pack for push */
    fprintf(stderr, "git-remote-https: push not yet implemented\n");
    printf("\n");
    fflush(stdout);
}

/* ============================================
 * Main
 * ============================================ */

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: git-remote-https <remote> <url>\n");
        fprintf(stderr, "\nThis is a git remote helper for HTTPS on Tiger.\n");
        fprintf(stderr, "It uses mbedTLS to provide TLS 1.2 support.\n");
        return 1;
    }

    const char* remote = argv[1];
    const char* url_str = argv[2];

    GitURL url;
    if (!parse_git_url(url_str, &url)) {
        fprintf(stderr, "git-remote-https: Invalid URL: %s\n", url_str);
        return 1;
    }

    /* Read commands from stdin (git remote helper protocol) */
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[--len] = '\0';
        }

        if (len == 0) {
            /* Empty line = end of batch */
            printf("\n");
            fflush(stdout);
            continue;
        }

        if (strcmp(line, "capabilities") == 0) {
            handle_capabilities();
        }
        else if (strcmp(line, "list") == 0) {
            handle_list(&url, 0);
        }
        else if (strcmp(line, "list for-push") == 0) {
            handle_list(&url, 1);
        }
        else if (strncmp(line, "fetch ", 6) == 0) {
            char sha1[41], ref[256];
            if (sscanf(line + 6, "%40s %255s", sha1, ref) == 2) {
                handle_fetch(&url, sha1, ref);
            }
        }
        else if (strncmp(line, "push ", 5) == 0) {
            char src[256], dst[256];
            if (sscanf(line + 5, "%255[^:]:%255s", src, dst) == 2) {
                handle_push(&url, src, dst);
            }
        }
        else if (strncmp(line, "option ", 7) == 0) {
            /* Options like "option verbosity 1" */
            printf("unsupported\n");
            fflush(stdout);
        }
        else {
            fprintf(stderr, "git-remote-https: Unknown command: %s\n", line);
        }
    }

    return 0;
}
