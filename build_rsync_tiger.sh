#!/bin/bash
# Build modern rsync for PowerPC Mac OS X Tiger
# rsync 3.x with xxHash for faster checksums

set -e

RSYNC_VERSION="3.2.7"
XXHASH_VERSION="0.8.2"
PREFIX="/usr/local"

echo "=== Building rsync $RSYNC_VERSION for Tiger ==="

# Check for wget (our custom one with TLS 1.2)
if [ ! -x /usr/local/bin/wget ] && [ ! -x ./wget ]; then
    echo "ERROR: wget with TLS support not found"
    echo "Please build wget_tiger first"
    exit 1
fi

WGET="/usr/local/bin/wget"
[ -x ./wget ] && WGET="./wget"

# Create build directory
mkdir -p ~/rsync_build
cd ~/rsync_build

# ============================================
# Step 1: Build xxHash (optional but recommended)
# ============================================
echo ""
echo "=== Step 1: Building xxHash $XXHASH_VERSION ==="

if [ ! -f "xxHash-$XXHASH_VERSION.tar.gz" ]; then
    echo "Downloading xxHash..."
    $WGET -O xxHash-$XXHASH_VERSION.tar.gz \
        "https://github.com/Cyan4973/xxHash/archive/refs/tags/v$XXHASH_VERSION.tar.gz"
fi

if [ ! -d "xxHash-$XXHASH_VERSION" ]; then
    echo "Extracting xxHash..."
    tar xzf xxHash-$XXHASH_VERSION.tar.gz
fi

cd xxHash-$XXHASH_VERSION

if [ ! -f "$PREFIX/lib/libxxhash.a" ]; then
    echo "Building xxHash..."
    make CC="gcc -arch ppc" CFLAGS="-O2 -mcpu=7450" lib

    echo "Installing xxHash..."
    sudo mkdir -p $PREFIX/include $PREFIX/lib
    sudo cp xxhash.h xxh3.h $PREFIX/include/
    sudo cp libxxhash.a $PREFIX/lib/
else
    echo "xxHash already installed, skipping..."
fi

cd ~/rsync_build

# ============================================
# Step 2: Build rsync
# ============================================
echo ""
echo "=== Step 2: Building rsync $RSYNC_VERSION ==="

if [ ! -f "rsync-$RSYNC_VERSION.tar.gz" ]; then
    echo "Downloading rsync..."
    $WGET -O rsync-$RSYNC_VERSION.tar.gz \
        "https://download.samba.org/pub/rsync/src/rsync-$RSYNC_VERSION.tar.gz"
fi

if [ ! -d "rsync-$RSYNC_VERSION" ]; then
    echo "Extracting rsync..."
    tar xzf rsync-$RSYNC_VERSION.tar.gz
fi

cd rsync-$RSYNC_VERSION

echo "Configuring rsync..."
./configure \
    --prefix=$PREFIX \
    --disable-md2man \
    --disable-openssl \
    --with-included-zlib \
    --with-included-popt \
    CC="gcc -arch ppc" \
    CFLAGS="-O2 -mcpu=7450 -I$PREFIX/include" \
    LDFLAGS="-L$PREFIX/lib"

echo "Building rsync..."
make -j2

echo "Installing rsync..."
sudo make install

echo ""
echo "=== rsync $RSYNC_VERSION installed successfully! ===\"
echo ""
echo "New rsync installed to $PREFIX/bin/rsync"
echo ""
echo "Test with:"
echo "  $PREFIX/bin/rsync --version"
echo ""
echo "Example usage:"
echo "  # Sync local directories"
echo "  rsync -avz ~/Documents/ /Volumes/Backup/Documents/"
echo ""
echo "  # Sync over SSH (uses new OpenSSH if installed)"
echo "  rsync -avz -e '$PREFIX/bin/ssh' ~/Files/ user@server:backup/"
echo ""
echo "  # Use xxHash for faster checksums (if supported)"
echo "  rsync -avz --checksum-choice=xxh3 ~/big_folder/ /backup/"
echo ""
