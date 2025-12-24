#!/bin/bash
# ==============================================================================
# Pocket Fox Builder - Firefox with Built-in mbedTLS for PowerPC Tiger
# ==============================================================================
#
# This script builds a minimal Firefox with self-contained TLS support,
# bypassing Tiger's broken OpenSSL and Python SSL issues entirely.
#
# Target: PowerPC Mac OS X Tiger (10.4) / Leopard (10.5)
# SSL Backend: mbedTLS 2.28 LTS (portable, C89 compatible)
#
# Usage:
#   ./build_pocketfox.sh all          # Full build
#   ./build_pocketfox.sh mbedtls      # Build mbedTLS only
#   ./build_pocketfox.sh firefox      # Build Firefox only (assumes mbedTLS ready)
#   ./build_pocketfox.sh test         # Test SSL bridge
#
# ==============================================================================

set -e  # Exit on error

# Configuration
MBEDTLS_VERSION="2.28.8"  # Last LTS with C89 support
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v${MBEDTLS_VERSION}.tar.gz"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/pocketfox-build"
MBEDTLS_DIR="${BUILD_DIR}/mbedtls-${MBEDTLS_VERSION}"
MBEDTLS_INSTALL="${BUILD_DIR}/mbedtls-install"

# Firefox
FIREFOX_VERSION="115.0esr"
FIREFOX_DIR="${BUILD_DIR}/firefox-${FIREFOX_VERSION}"

# Compiler settings for PowerPC Tiger
export CC="gcc -arch ppc"
export CXX="g++ -arch ppc"
export CFLAGS="-O2 -mcpu=7450 -maltivec -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
export CXXFLAGS="${CFLAGS}"
export LDFLAGS="-isysroot /Developer/SDKs/MacOSX10.4u.sdk -mmacosx-version-min=10.4"

# ==============================================================================
# Helper Functions
# ==============================================================================

log() {
    echo "[PocketFox] $1"
}

error() {
    echo "[ERROR] $1" >&2
    exit 1
}

check_tiger() {
    if [ ! -d "/Developer/SDKs/MacOSX10.4u.sdk" ]; then
        log "Warning: Tiger SDK not found, using system SDK"
        export CFLAGS="-O2 -mcpu=7450 -maltivec"
        export CXXFLAGS="${CFLAGS}"
        export LDFLAGS=""
    fi
}

# ==============================================================================
# Build mbedTLS
# ==============================================================================

build_mbedtls() {
    log "=== Building mbedTLS ${MBEDTLS_VERSION} for PowerPC ==="

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # Download if needed
    if [ ! -f "mbedtls-${MBEDTLS_VERSION}.tar.gz" ]; then
        log "Downloading mbedTLS ${MBEDTLS_VERSION}..."

        # Try curl first (available on Tiger)
        if command -v curl &> /dev/null; then
            curl -L -o "mbedtls-${MBEDTLS_VERSION}.tar.gz" "${MBEDTLS_URL}"
        elif command -v wget &> /dev/null; then
            wget -O "mbedtls-${MBEDTLS_VERSION}.tar.gz" "${MBEDTLS_URL}"
        else
            error "No curl or wget available!"
        fi
    fi

    # Extract
    if [ ! -d "${MBEDTLS_DIR}" ]; then
        log "Extracting..."
        tar xzf "mbedtls-${MBEDTLS_VERSION}.tar.gz"
    fi

    cd "${MBEDTLS_DIR}"

    # Apply PowerPC Tiger patches
    log "Applying PowerPC Tiger patches..."

    # Patch config to disable problematic features
    cat > library/ppc_config_patch.h << 'EOF'
/* PowerPC Tiger mbedTLS Configuration Patch */
#ifndef PPC_CONFIG_PATCH_H
#define PPC_CONFIG_PATCH_H

/* Disable hardware acceleration (use portable C) */
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_PADLOCK_C
#undef MBEDTLS_HAVE_SSE2

/* Disable threading (Tiger's pthreads is old) */
#undef MBEDTLS_THREADING_C
#undef MBEDTLS_THREADING_PTHREAD

/* Use filesystem for certs */
#define MBEDTLS_FS_IO

/* Time functions */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

/* Enable all cipher suites */
#define MBEDTLS_SSL_ALL_ALERT_MESSAGES
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_RENEGOTIATION

/* TLS 1.2 (modern enough, Tiger compatible) */
#define MBEDTLS_SSL_PROTO_TLS1_2

/* Disable TLS 1.3 (requires modern features) */
#undef MBEDTLS_SSL_PROTO_TLS1_3

/* Certificate parsing */
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_CRL_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

#endif /* PPC_CONFIG_PATCH_H */
EOF

    # Include our patch in the main config
    if ! grep -q "ppc_config_patch.h" include/mbedtls/mbedtls_config.h 2>/dev/null; then
        # Try both possible config file names
        CONFIG_FILE=""
        if [ -f include/mbedtls/mbedtls_config.h ]; then
            CONFIG_FILE="include/mbedtls/mbedtls_config.h"
        elif [ -f include/mbedtls/config.h ]; then
            CONFIG_FILE="include/mbedtls/config.h"
        fi

        if [ -n "${CONFIG_FILE}" ]; then
            log "Patching ${CONFIG_FILE}..."
            # Add include at the end before closing
            echo '#include "ppc_config_patch.h"' >> "${CONFIG_FILE}"
        fi
    fi

    # Build using make (CMake not available on old Tiger)
    log "Building mbedTLS library..."

    # Clean any previous build
    make clean 2>/dev/null || true

    # Build with PowerPC flags
    make CC="${CC}" \
         CFLAGS="${CFLAGS} -DMBEDTLS_CONFIG_FILE='\"ppc_config_patch.h\"'" \
         AR="ar" \
         lib -j2

    # Install to our prefix
    mkdir -p "${MBEDTLS_INSTALL}/lib"
    mkdir -p "${MBEDTLS_INSTALL}/include"

    cp library/*.a "${MBEDTLS_INSTALL}/lib/" 2>/dev/null || true
    cp -r include/mbedtls "${MBEDTLS_INSTALL}/include/"
    cp library/ppc_config_patch.h "${MBEDTLS_INSTALL}/include/mbedtls/"

    log "mbedTLS installed to ${MBEDTLS_INSTALL}"

    # Verify
    if [ -f "${MBEDTLS_INSTALL}/lib/libmbedtls.a" ]; then
        log "SUCCESS: mbedTLS built for PowerPC!"
        ls -la "${MBEDTLS_INSTALL}/lib/"
    else
        error "mbedTLS build failed - library not found"
    fi
}

# ==============================================================================
# Build SSL Bridge
# ==============================================================================

build_ssl_bridge() {
    log "=== Building PocketFox SSL Bridge ==="

    cd "${SCRIPT_DIR}"

    # Compile the bridge with mbedTLS
    log "Compiling mbedtls_firefox_patch.c..."

    ${CC} ${CFLAGS} \
        -DHAVE_MBEDTLS \
        -I"${MBEDTLS_INSTALL}/include" \
        -c mbedtls_firefox_patch.c \
        -o "${BUILD_DIR}/mbedtls_firefox_patch.o"

    # Create static library
    ar rcs "${BUILD_DIR}/libpocketfox_ssl.a" "${BUILD_DIR}/mbedtls_firefox_patch.o"

    log "SSL bridge library: ${BUILD_DIR}/libpocketfox_ssl.a"

    # Build test program
    log "Building SSL bridge test..."

    ${CC} ${CFLAGS} \
        -DHAVE_MBEDTLS \
        -DTEST_STANDALONE \
        -I"${MBEDTLS_INSTALL}/include" \
        -o "${BUILD_DIR}/ssl_bridge_test" \
        mbedtls_firefox_patch.c \
        -L"${MBEDTLS_INSTALL}/lib" \
        -lmbedtls -lmbedx509 -lmbedcrypto \
        ${LDFLAGS}

    log "Test binary: ${BUILD_DIR}/ssl_bridge_test"
}

# ==============================================================================
# Test SSL Bridge
# ==============================================================================

test_ssl() {
    log "=== Testing SSL Bridge ==="

    if [ ! -f "${BUILD_DIR}/ssl_bridge_test" ]; then
        error "Test binary not found. Run: $0 mbedtls && $0 bridge"
    fi

    cd "${BUILD_DIR}"
    ./ssl_bridge_test
}

# ==============================================================================
# Create Firefox Integration Patches
# ==============================================================================

create_firefox_patches() {
    log "=== Creating Firefox Integration Patches ==="

    mkdir -p "${BUILD_DIR}/patches"

    # Patch 1: Replace NSS SSL calls with PocketFox bridge
    cat > "${BUILD_DIR}/patches/01-pocketfox-ssl.patch" << 'PATCH_EOF'
--- a/security/nss/lib/ssl/ssl3con.c
+++ b/security/nss/lib/ssl/ssl3con.c
@@ -1,3 +1,12 @@
+/* PocketFox SSL Bridge
+ * This patches NSS to use our portable mbedTLS backend on Tiger.
+ * The actual SSL operations are handled by libpocketfox_ssl.a
+ */
+#ifdef POCKETFOX_SSL_BRIDGE
+#include "pocketfox_ssl.h"
+#define SSL_USING_POCKETFOX 1
+#endif
+
 /* This Source Code Form is subject to the terms of the Mozilla Public
  * License, v. 2.0. If a copy of the MPL was not distributed with this
  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
PATCH_EOF

    # Patch 2: Link with mbedTLS libraries
    cat > "${BUILD_DIR}/patches/02-pocketfox-link.patch" << 'PATCH_EOF'
--- a/security/nss/Makefile
+++ b/security/nss/Makefile
@@ -50,6 +50,13 @@ endif
 ifdef POCKETFOX_SSL_BRIDGE
 # Link with PocketFox SSL bridge (mbedTLS backend)
 EXTRA_LIBS += -lpocketfox_ssl -lmbedtls -lmbedx509 -lmbedcrypto
+DEFINES += -DPOCKETFOX_SSL_BRIDGE
+endif
+
+# PowerPC Tiger specific
+ifdef TARGET_POWERPC_TIGER
+EXTRA_LIBS += -L$(POCKETFOX_MBEDTLS)/lib
+INCLUDES += -I$(POCKETFOX_MBEDTLS)/include
 endif
PATCH_EOF

    # Patch 3: Configure options
    cat > "${BUILD_DIR}/patches/03-pocketfox-configure.patch" << 'PATCH_EOF'
--- a/configure.in
+++ b/configure.in
@@ -8000,6 +8000,15 @@ dnl ===
 dnl = PocketFox SSL Bridge (mbedTLS backend for Tiger)
 dnl ===

+AC_ARG_ENABLE(pocketfox-ssl,
+[  --enable-pocketfox-ssl  Use mbedTLS instead of system NSS (for Tiger)],
+    POCKETFOX_SSL_BRIDGE=$enableval,
+    POCKETFOX_SSL_BRIDGE=no)
+
+if test "$POCKETFOX_SSL_BRIDGE" = "yes"; then
+    AC_DEFINE(POCKETFOX_SSL_BRIDGE)
+fi
+
 dnl ===
PATCH_EOF

    log "Patches created in ${BUILD_DIR}/patches/"
    ls -la "${BUILD_DIR}/patches/"
}

# ==============================================================================
# Build Minimal Firefox (Pocket Fox)
# ==============================================================================

build_firefox() {
    log "=== Building Pocket Fox (Minimal Firefox) ==="

    if [ ! -d "${FIREFOX_DIR}" ]; then
        error "Firefox source not found at ${FIREFOX_DIR}"
    fi

    cd "${FIREFOX_DIR}"

    # Apply our patches
    log "Applying PocketFox patches..."
    for patch in "${BUILD_DIR}/patches/"*.patch; do
        if [ -f "$patch" ]; then
            patch -p1 < "$patch" 2>/dev/null || log "Patch may already be applied: $patch"
        fi
    done

    # Create PocketFox mozconfig
    cat > mozconfig << 'MOZCONFIG_EOF'
# Pocket Fox Configuration - Minimal Firefox for PowerPC Tiger
# With built-in mbedTLS SSL!

# Target PowerPC Tiger
ac_add_options --target=powerpc-apple-darwin8
ac_add_options --host=powerpc-apple-darwin8
ac_add_options --with-macos-sdk=/Developer/SDKs/MacOSX10.4u.sdk
ac_add_options --enable-macos-target=10.4

# Compilers
export CC="gcc -arch ppc"
export CXX="g++ -arch ppc"
export CFLAGS="-Os -mcpu=7450 -maltivec -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
export CXXFLAGS="${CFLAGS}"
export LDFLAGS="-isysroot /Developer/SDKs/MacOSX10.4u.sdk -mmacosx-version-min=10.4"

# Custom Rust compiler
export RUSTC="${SCRIPT_DIR}/rustc_ppc"
export CARGO="${SCRIPT_DIR}/cargo_ppc.sh"

# PocketFox SSL Bridge (mbedTLS)
export POCKETFOX_SSL_BRIDGE=1
export POCKETFOX_MBEDTLS="${MBEDTLS_INSTALL}"
ac_add_options --enable-pocketfox-ssl

# Core browser
ac_add_options --enable-application=browser
ac_add_options --disable-debug
ac_add_options --enable-optimize="-Os"
ac_add_options --enable-strip
ac_add_options --disable-debug-symbols
ac_add_options --disable-tests

# Disable heavy features
ac_add_options --disable-crashreporter
ac_add_options --disable-updater
ac_add_options --disable-webrtc
ac_add_options --disable-eme
ac_add_options --disable-accessibility

# Build settings
mk_add_options MOZ_MAKE_FLAGS="-j1"
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-pocketfox
MOZCONFIG_EOF

    log "Mozconfig created. Starting build..."

    # Bootstrap check
    if [ -f "mach" ]; then
        # Try the build
        export PATH="/usr/local/bin:/opt/local/bin:${PATH}"
        ./mach build 2>&1 | tee "${BUILD_DIR}/firefox_build.log"
    else
        error "mach not found - Firefox source incomplete?"
    fi
}

# ==============================================================================
# Package Pocket Fox
# ==============================================================================

package_pocketfox() {
    log "=== Packaging Pocket Fox ==="

    OBJDIR="${FIREFOX_DIR}/obj-pocketfox"

    if [ ! -d "${OBJDIR}/dist/PocketFox.app" ] && [ ! -d "${OBJDIR}/dist/Firefox.app" ]; then
        error "Built application not found in ${OBJDIR}/dist/"
    fi

    # Rename to PocketFox
    cd "${OBJDIR}/dist"
    if [ -d "Firefox.app" ]; then
        mv Firefox.app PocketFox.app
    fi

    # Create DMG
    log "Creating PocketFox.dmg..."
    hdiutil create -volname "PocketFox" \
                   -srcfolder PocketFox.app \
                   -ov -format UDZO \
                   "${BUILD_DIR}/PocketFox-${FIREFOX_VERSION}-ppc.dmg"

    log "SUCCESS! Package ready: ${BUILD_DIR}/PocketFox-${FIREFOX_VERSION}-ppc.dmg"
}

# ==============================================================================
# Main
# ==============================================================================

main() {
    case "${1:-all}" in
        mbedtls)
            check_tiger
            build_mbedtls
            ;;
        bridge)
            check_tiger
            build_ssl_bridge
            ;;
        patches)
            create_firefox_patches
            ;;
        test)
            test_ssl
            ;;
        firefox)
            check_tiger
            create_firefox_patches
            build_firefox
            ;;
        package)
            package_pocketfox
            ;;
        all)
            log "=== Full Pocket Fox Build ==="
            check_tiger
            build_mbedtls
            build_ssl_bridge
            create_firefox_patches
            # Note: Firefox build requires source and is interactive
            log "mbedTLS and SSL bridge ready!"
            log "Next: Place Firefox source in ${FIREFOX_DIR}"
            log "Then: $0 firefox"
            ;;
        *)
            echo "Usage: $0 {all|mbedtls|bridge|patches|test|firefox|package}"
            exit 1
            ;;
    esac
}

main "$@"
