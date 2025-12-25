#!/bin/bash
# Build curl with mbedTLS for PowerPC Mac OS X Tiger
# This enables git to use HTTPS with TLS 1.2!

set -e

CURL_VERSION="7.88.1"  # Last version with good PowerPC support
MBEDTLS_DIR="$HOME/mbedtls-2.28.8"

echo "=== Building curl $CURL_VERSION with mbedTLS for Tiger ==="

# Check for mbedTLS
if [ ! -d "$MBEDTLS_DIR/library" ]; then
    echo "ERROR: mbedTLS not found at $MBEDTLS_DIR"
    echo "Please build mbedTLS first (see README.md)"
    exit 1
fi

# Download curl if needed
if [ ! -d "curl-$CURL_VERSION" ]; then
    echo "Downloading curl $CURL_VERSION..."
    curl -L -o curl.tar.gz "https://curl.se/download/curl-$CURL_VERSION.tar.gz"
    tar xzf curl.tar.gz
fi

cd "curl-$CURL_VERSION"

# Configure for PowerPC Tiger with mbedTLS
echo "Configuring curl..."
./configure \
    --host=powerpc-apple-darwin8 \
    --prefix=/usr/local \
    --with-mbedtls="$MBEDTLS_DIR" \
    --without-ssl \
    --without-gnutls \
    --without-nss \
    --without-libssh2 \
    --disable-ldap \
    --disable-ldaps \
    --disable-rtsp \
    --disable-dict \
    --disable-telnet \
    --disable-tftp \
    --disable-pop3 \
    --disable-imap \
    --disable-smb \
    --disable-smtp \
    --disable-gopher \
    --disable-mqtt \
    CC="gcc -arch ppc" \
    CFLAGS="-O2 -mcpu=7450 -I$MBEDTLS_DIR/include" \
    LDFLAGS="-L$MBEDTLS_DIR/library" \
    LIBS="-lmbedtls -lmbedx509 -lmbedcrypto"

echo "Building curl..."
make -j2

echo "Installing curl..."
sudo make install

echo ""
echo "=== curl with mbedTLS installed! ==="
echo ""
echo "Test with:"
echo "  /usr/local/bin/curl --version"
echo "  /usr/local/bin/curl https://github.com"
echo ""
echo "For git to use it, add to ~/.gitconfig:"
echo "  [http]"
echo "      sslBackend = mbedtls"
echo ""
echo "Or set GIT_CURL_PATH:"
echo "  export GIT_CURL_PATH=/usr/local/bin/curl"
