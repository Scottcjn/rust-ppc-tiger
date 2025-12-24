/*
 * PocketFox SSL Bridge API
 * Public interface for Firefox integration
 *
 * This header provides NSS-compatible function signatures that map
 * to our mbedTLS backend, allowing Firefox to use modern TLS on
 * Mac OS X Tiger without requiring system OpenSSL/Python SSL.
 *
 * Usage in Firefox code:
 *   #ifdef POCKETFOX_SSL_BRIDGE
 *   #include "pocketfox_ssl.h"
 *   // Use PF_* functions instead of NSS PR_* functions
 *   #endif
 */

#ifndef POCKETFOX_SSL_H
#define POCKETFOX_SSL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque SSL context */
typedef struct PocketFoxSSL PocketFoxSSL;

/* ============================================
 * Initialization
 * ============================================ */

/**
 * Initialize the PocketFox SSL subsystem.
 * Call once at application startup.
 * @return 0 on success, -1 on failure
 */
int pocketfox_ssl_init(void);

/**
 * Shutdown the PocketFox SSL subsystem.
 * Call at application exit.
 */
void pocketfox_ssl_shutdown(void);

/* ============================================
 * Context Management
 * ============================================ */

/**
 * Create a new SSL context.
 * @return New context or NULL on failure
 */
PocketFoxSSL* pocketfox_ssl_new(void);

/**
 * Free an SSL context.
 * @param ctx Context to free (may be NULL)
 */
void pocketfox_ssl_free(PocketFoxSSL* ctx);

/* ============================================
 * Connection API
 * ============================================ */

/**
 * Connect to a TLS server.
 * @param ctx SSL context
 * @param hostname Server hostname
 * @param port Server port (usually 443)
 * @return 0 on success, -1 on failure
 */
int pocketfox_ssl_connect(PocketFoxSSL* ctx, const char* hostname, int port);

/**
 * Read data from SSL connection.
 * @param ctx SSL context
 * @param buf Buffer to read into
 * @param len Maximum bytes to read
 * @return Bytes read, or -1 on error
 */
int pocketfox_ssl_read(PocketFoxSSL* ctx, unsigned char* buf, size_t len);

/**
 * Write data to SSL connection.
 * @param ctx SSL context
 * @param buf Data to write
 * @param len Bytes to write
 * @return Bytes written, or -1 on error
 */
int pocketfox_ssl_write(PocketFoxSSL* ctx, const unsigned char* buf, size_t len);

/**
 * Close SSL connection.
 * @param ctx SSL context
 */
void pocketfox_ssl_close(PocketFoxSSL* ctx);

/**
 * Get last error message.
 * @param ctx SSL context
 * @return Error string (do not free)
 */
const char* pocketfox_ssl_error(PocketFoxSSL* ctx);

/* ============================================
 * Certificate Management
 * ============================================ */

/**
 * Load CA certificate bundle.
 * @param ctx SSL context
 * @param path Path to CA bundle file (PEM format)
 * @return 0 on success, -1 on failure
 */
int pocketfox_ssl_load_ca_bundle(PocketFoxSSL* ctx, const char* path);

/* ============================================
 * NSS Compatibility Shims
 * These provide drop-in replacements for NSS functions
 * ============================================ */

/**
 * PR_Read replacement - read from SSL socket
 */
int PF_SSL_Read(void* ssl_ctx, void* buf, int amount);

/**
 * PR_Write replacement - write to SSL socket
 */
int PF_SSL_Write(void* ssl_ctx, const void* buf, int amount);

/**
 * SSL_ImportFD replacement - create SSL layer
 */
void* PF_SSL_ImportFD(void* model, void* fd);

/**
 * SSL_SetURL replacement - set server hostname
 */
int PF_SSL_SetURL(void* ssl_ctx, const char* url);

/* ============================================
 * Convenience Macros for Firefox Integration
 * ============================================ */

#ifdef POCKETFOX_SSL_BRIDGE
/* Remap NSS calls to PocketFox bridge */
#define PR_Read(fd, buf, amount)    PF_SSL_Read(fd, buf, amount)
#define PR_Write(fd, buf, amount)   PF_SSL_Write(fd, buf, amount)
#define SSL_ImportFD(model, fd)     PF_SSL_ImportFD(model, fd)
#define SSL_SetURL(fd, url)         PF_SSL_SetURL(fd, url)
#endif

#ifdef __cplusplus
}
#endif

#endif /* POCKETFOX_SSL_H */
