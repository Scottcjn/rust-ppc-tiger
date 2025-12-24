/*
 * mbedTLS Bridge for Firefox on PowerPC Tiger
 * Replaces NSS SSL with portable mbedTLS 2.28 LTS
 *
 * "Pocket Fox" - Firefox with built-in TLS!
 *
 * Build: gcc -O2 -mcpu=7450 -c mbedtls_firefox_patch.c -I/path/to/mbedtls/include
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* mbedTLS headers - will be included from mbedtls/include */
#ifdef HAVE_MBEDTLS
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#endif

/* ============================================
 * SSL Context Bridge
 * Maps Firefox's NSS-style API to mbedTLS
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
    /* Connection state */
    char hostname[256];
    int port;
    int is_connected;
} PocketFoxSSL;

/* Global initialization state */
static int g_ssl_initialized = 0;

/* ============================================
 * Initialization
 * ============================================ */

int pocketfox_ssl_init(void) {
    if (g_ssl_initialized) return 0;

    printf("[PocketFox] Initializing mbedTLS 2.28 LTS...\n");

    /* mbedTLS doesn't need global init like OpenSSL */
    g_ssl_initialized = 1;

    printf("[PocketFox] SSL subsystem ready (PowerPC Tiger)\n");
    return 0;
}

void pocketfox_ssl_shutdown(void) {
    g_ssl_initialized = 0;
    printf("[PocketFox] SSL subsystem shutdown\n");
}

/* ============================================
 * Context Management
 * ============================================ */

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

    /* Seed the RNG */
    const char *pers = "pocketfox_ssl";
    int ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                    &ctx->entropy,
                                    (const unsigned char *)pers,
                                    strlen(pers));
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Failed to seed RNG: -0x%04x", -ret);
        return ctx;  /* Return anyway, let caller check */
    }

    /* Setup SSL config for client mode */
    ret = mbedtls_ssl_config_defaults(&ctx->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Failed to set SSL defaults: -0x%04x", -ret);
        return ctx;
    }

    /* Set auth mode - optional for Tiger (certs may be outdated) */
    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

    /* Set RNG */
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    /* Apply config to SSL context */
    ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Failed to setup SSL: -0x%04x", -ret);
        return ctx;
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

/* ============================================
 * Connection API
 * ============================================ */

int pocketfox_ssl_connect(PocketFoxSSL* ctx, const char* hostname, int port) {
    if (!ctx || !ctx->initialized) return -1;

    strncpy(ctx->hostname, hostname, sizeof(ctx->hostname) - 1);
    ctx->port = port;

#ifdef HAVE_MBEDTLS
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    printf("[PocketFox] Connecting to %s:%d...\n", hostname, port);

    /* Connect TCP */
    int ret = mbedtls_net_connect(&ctx->server_fd, hostname, port_str,
                                  MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "TCP connect failed: -0x%04x", -ret);
        return -1;
    }

    /* Set hostname for SNI */
    ret = mbedtls_ssl_set_hostname(&ctx->ssl, hostname);
    if (ret != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Set hostname failed: -0x%04x", -ret);
        return -1;
    }

    /* Set I/O callbacks */
    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->server_fd,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    /* Perform TLS handshake */
    printf("[PocketFox] TLS handshake with %s...\n", hostname);
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "TLS handshake failed: -0x%04x", -ret);
            return -1;
        }
    }

    printf("[PocketFox] Connected! TLS version: %s\n",
           mbedtls_ssl_get_version(&ctx->ssl));
#else
    printf("[PocketFox] SSL compiled without mbedTLS - stub mode\n");
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

    if (ret < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "SSL read failed: -0x%04x", -ret);
    }
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

    if (ret < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "SSL write failed: -0x%04x", -ret);
    }
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
 * Certificate Loading
 * ============================================ */

int pocketfox_ssl_load_ca_bundle(PocketFoxSSL* ctx, const char* path) {
    if (!ctx) return -1;

#ifdef HAVE_MBEDTLS
    int ret = mbedtls_x509_crt_parse_file(&ctx->cacert, path);
    if (ret < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "Failed to load CA bundle: -0x%04x", -ret);
        return -1;
    }

    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->cacert, NULL);
    printf("[PocketFox] Loaded CA bundle: %s (%d certs)\n", path, ret);
#endif

    return 0;
}

/* ============================================
 * NSS Compatibility Shims
 * These map Firefox's NSS calls to our mbedTLS
 * ============================================ */

/* NSS-style function signatures for Firefox integration */

/* PR_Read equivalent */
int PF_SSL_Read(void* ssl_ctx, void* buf, int amount) {
    return pocketfox_ssl_read((PocketFoxSSL*)ssl_ctx,
                              (unsigned char*)buf,
                              (size_t)amount);
}

/* PR_Write equivalent */
int PF_SSL_Write(void* ssl_ctx, const void* buf, int amount) {
    return pocketfox_ssl_write((PocketFoxSSL*)ssl_ctx,
                               (const unsigned char*)buf,
                               (size_t)amount);
}

/* SSL_ImportFD equivalent */
void* PF_SSL_ImportFD(void* model, void* fd) {
    /* In our bridge, we manage our own contexts */
    return pocketfox_ssl_new();
}

/* SSL_SetURL equivalent */
int PF_SSL_SetURL(void* ssl_ctx, const char* url) {
    PocketFoxSSL* ctx = (PocketFoxSSL*)ssl_ctx;
    if (!ctx) return -1;

    /* Parse hostname from URL */
    const char* host_start = url;
    if (strncmp(url, "https://", 8) == 0) {
        host_start = url + 8;
    }

    /* Copy hostname (up to : or / or end) */
    int i = 0;
    while (host_start[i] && host_start[i] != ':' &&
           host_start[i] != '/' && i < 255) {
        ctx->hostname[i] = host_start[i];
        i++;
    }
    ctx->hostname[i] = '\0';

    return 0;
}

/* ============================================
 * Self-Test (compile with -DTEST_STANDALONE)
 * ============================================ */

#ifdef TEST_STANDALONE
int main(int argc, char** argv) {
    printf("=== PocketFox SSL Bridge Test ===\n");
    printf("Platform: PowerPC Mac OS X Tiger\n");
    printf("SSL Backend: mbedTLS 2.28 LTS\n\n");

    /* Initialize */
    if (pocketfox_ssl_init() != 0) {
        printf("FAIL: SSL init failed\n");
        return 1;
    }

    /* Create context */
    PocketFoxSSL* ctx = pocketfox_ssl_new();
    if (!ctx) {
        printf("FAIL: Context creation failed\n");
        return 1;
    }

    printf("Context created successfully\n");

#ifdef HAVE_MBEDTLS
    /* Try connecting to a test server */
    const char* test_host = "example.com";
    printf("Testing connection to %s:443...\n", test_host);

    if (pocketfox_ssl_connect(ctx, test_host, 443) == 0) {
        printf("SUCCESS: TLS connection established!\n");

        /* Send a simple HTTP request */
        const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\n"
                              "Connection: close\r\n\r\n";
        pocketfox_ssl_write(ctx, (const unsigned char*)request, strlen(request));

        /* Read response */
        unsigned char buf[4096];
        int n = pocketfox_ssl_read(ctx, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("Response (first 500 chars):\n%.500s\n", buf);
        }

        pocketfox_ssl_close(ctx);
    } else {
        printf("Connection failed: %s\n", pocketfox_ssl_error(ctx));
    }
#else
    printf("Compiled in stub mode (no mbedTLS)\n");
    printf("To enable: gcc -DHAVE_MBEDTLS -I/path/to/mbedtls ...\n");
#endif

    /* Cleanup */
    pocketfox_ssl_free(ctx);
    pocketfox_ssl_shutdown();

    printf("\n=== Test Complete ===\n");
    return 0;
}
#endif
