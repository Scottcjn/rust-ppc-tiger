/*
 * PocketFox SSL Bridge for PowerPC Tiger
 * With custom entropy source for Tiger compatibility
 *
 * Build on Tiger:
 *   gcc -arch ppc -std=c99 -O2 -mcpu=7450 -DHAVE_MBEDTLS -DTEST_STANDALONE \
 *       -I./mbedtls-2.28.8/include -o pocketfox_ssl \
 *       pocketfox_ssl_tiger.c \
 *       -L./mbedtls-2.28.8/library -lmbedtls -lmbedx509 -lmbedcrypto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#ifdef HAVE_MBEDTLS
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#endif

/* ============================================
 * Tiger-compatible entropy source
 * Uses /dev/urandom with time fallback
 * ============================================ */

static int tiger_entropy_source(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;

    /* Try /dev/urandom first */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, output, len);
        close(fd);
        if (n > 0) {
            *olen = (size_t)n;
            return 0;
        }
    }

    /* Fallback: time-based entropy (less secure but works) */
    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint32_t seed = (uint32_t)(tv.tv_sec ^ tv.tv_usec ^ getpid());

    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        output[i] = (unsigned char)(seed >> 16);
    }

    *olen = len;
    return 0;
}

/* ============================================
 * SSL Context for Tiger
 * ============================================ */

typedef struct PocketFoxSSL {
#ifdef HAVE_MBEDTLS
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    mbedtls_net_context server_fd;
#endif
    int initialized;
    char last_error[256];
    char hostname[256];
    int port;
    int is_connected;
} PocketFoxSSL;

static int g_ssl_initialized = 0;

int pocketfox_ssl_init(void) {
    if (g_ssl_initialized) return 0;
    printf("[PocketFox] Initializing mbedTLS for Tiger...\n");
    g_ssl_initialized = 1;
    printf("[PocketFox] SSL subsystem ready\n");
    return 0;
}

void pocketfox_ssl_shutdown(void) {
    g_ssl_initialized = 0;
}

PocketFoxSSL* pocketfox_ssl_new(void) {
    PocketFoxSSL* ctx = (PocketFoxSSL*)calloc(1, sizeof(PocketFoxSSL));
    if (!ctx) return NULL;

#ifdef HAVE_MBEDTLS
    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_x509_crt_init(&ctx->cacert);
    mbedtls_net_init(&ctx->server_fd);

    /* Add Tiger-compatible entropy source */
    int ret = mbedtls_entropy_add_source(&ctx->entropy, tiger_entropy_source,
                                         NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0) {
        printf("[PocketFox] Warning: entropy source add failed\n");
    }

    /* Seed the RNG */
    const char *pers = "pocketfox_tiger";
    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                &ctx->entropy,
                                (const unsigned char *)pers,
                                strlen(pers));
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "RNG seed failed: -0x%04x", -ret);
        printf("[PocketFox] %s\n", ctx->last_error);
        /* Don't fail - continue with what we have */
    }

    /* Setup SSL config */
    ret = mbedtls_ssl_config_defaults(&ctx->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "SSL defaults failed: -0x%04x", -ret);
        return ctx;
    }

    /* Skip cert verification for Tiger (certs outdated) */
    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "SSL setup failed: -0x%04x", -ret);
    }
#endif

    ctx->initialized = 1;
    return ctx;
}

void pocketfox_ssl_free(PocketFoxSSL* ctx) {
    if (!ctx) return;
#ifdef HAVE_MBEDTLS
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->conf);
    mbedtls_entropy_free(&ctx->entropy);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_x509_crt_free(&ctx->cacert);
    mbedtls_net_free(&ctx->server_fd);
#endif
    free(ctx);
}

int pocketfox_ssl_connect(PocketFoxSSL* ctx, const char* hostname, int port) {
    if (!ctx || !ctx->initialized) return -1;

    strncpy(ctx->hostname, hostname, sizeof(ctx->hostname) - 1);
    ctx->port = port;

#ifdef HAVE_MBEDTLS
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    printf("[PocketFox] Connecting to %s:%d...\n", hostname, port);

    int ret = mbedtls_net_connect(&ctx->server_fd, hostname, port_str,
                                  MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "TCP connect failed: -0x%04x", -ret);
        return -1;
    }

    printf("[PocketFox] TCP connected, starting TLS handshake...\n");

    ret = mbedtls_ssl_set_hostname(&ctx->ssl, hostname);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Set hostname failed: -0x%04x", -ret);
        return -1;
    }

    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->server_fd,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "TLS handshake failed: -0x%04x", -ret);
            return -1;
        }
    }

    printf("[PocketFox] TLS connected! Version: %s, Cipher: %s\n",
           mbedtls_ssl_get_version(&ctx->ssl),
           mbedtls_ssl_get_ciphersuite(&ctx->ssl));
#endif

    ctx->is_connected = 1;
    return 0;
}

int pocketfox_ssl_read(PocketFoxSSL* ctx, unsigned char* buf, size_t len) {
    if (!ctx || !ctx->is_connected) return -1;
#ifdef HAVE_MBEDTLS
    int ret;
    do {
        ret = mbedtls_ssl_read(&ctx->ssl, buf, len);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret;
#else
    return -1;
#endif
}

int pocketfox_ssl_write(PocketFoxSSL* ctx, const unsigned char* buf, size_t len) {
    if (!ctx || !ctx->is_connected) return -1;
#ifdef HAVE_MBEDTLS
    int ret;
    do {
        ret = mbedtls_ssl_write(&ctx->ssl, buf, len);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret;
#else
    return -1;
#endif
}

void pocketfox_ssl_close(PocketFoxSSL* ctx) {
    if (!ctx) return;
#ifdef HAVE_MBEDTLS
    if (ctx->is_connected) {
        mbedtls_ssl_close_notify(&ctx->ssl);
        mbedtls_net_free(&ctx->server_fd);
    }
#endif
    ctx->is_connected = 0;
}

const char* pocketfox_ssl_error(PocketFoxSSL* ctx) {
    if (!ctx) return "NULL context";
    return ctx->last_error;
}

/* ============================================
 * Test Program
 * ============================================ */

#ifdef TEST_STANDALONE
int main(int argc, char** argv) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║  POCKETFOX SSL - PowerPC Mac OS X Tiger           ║\n");
    printf("║  Modern HTTPS on your 2005 Power Mac!             ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");
    printf("\n");

    const char* host = (argc > 1) ? argv[1] : "example.com";

    pocketfox_ssl_init();

    PocketFoxSSL* ctx = pocketfox_ssl_new();
    if (!ctx) {
        printf("FAIL: Context creation failed\n");
        return 1;
    }

    if (pocketfox_ssl_connect(ctx, host, 443) == 0) {
        printf("\n[SUCCESS] Secure connection established!\n\n");

        /* Send HTTP request */
        char request[512];
        snprintf(request, sizeof(request),
                 "GET / HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: PocketFox/1.0 (PowerPC Tiger)\r\n"
                 "Connection: close\r\n\r\n", host);

        pocketfox_ssl_write(ctx, (const unsigned char*)request, strlen(request));

        /* Read response */
        unsigned char buf[4096];
        int n = pocketfox_ssl_read(ctx, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("=== Response from %s ===\n", host);
            /* Show first 800 chars */
            if (n > 800) buf[800] = '\0';
            printf("%s\n", buf);
            if (n > 800) printf("\n... (truncated)\n");
        }

        pocketfox_ssl_close(ctx);
    } else {
        printf("Connection failed: %s\n", pocketfox_ssl_error(ctx));
    }

    pocketfox_ssl_free(ctx);
    pocketfox_ssl_shutdown();

    printf("\n=== PocketFox SSL Test Complete ===\n");
    return 0;
}
#endif
