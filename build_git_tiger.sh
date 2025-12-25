#!/bin/bash
# Build Git with HTTPS support for PowerPC Mac OS X Tiger
# Uses MacPorts curl for TLS 1.2 HTTPS!

set -e

GIT_VERSION="2.43.0"
PREFIX="/usr/local"

echo "=== Building Git $GIT_VERSION with HTTPS for Tiger ==="

# Check for download tool
DOWNLOAD=""
if [ -x /usr/local/bin/wget ]; then
    DOWNLOAD="/usr/local/bin/wget -O"
elif [ -x /opt/local/bin/curl ]; then
    DOWNLOAD="/opt/local/bin/curl -L -o"
else
    echo "ERROR: No download tool with TLS support found"
    exit 1
fi
echo "Using: $DOWNLOAD"

# Check for curl (needed for HTTPS)
if [ ! -x /opt/local/bin/curl-config ]; then
    echo "ERROR: MacPorts curl not found"
    echo "Install with: sudo port install curl"
    exit 1
fi
echo "Found curl: $(/opt/local/bin/curl --version | head -1)"

# Create build directory
mkdir -p ~/git_build
cd ~/git_build

# Download Git
if [ ! -f "git-$GIT_VERSION.tar.gz" ]; then
    echo "Downloading Git $GIT_VERSION..."
    $DOWNLOAD git-$GIT_VERSION.tar.gz \
        "https://mirrors.edge.kernel.org/pub/software/scm/git/git-$GIT_VERSION.tar.gz"
fi

if [ ! -d "git-$GIT_VERSION" ]; then
    echo "Extracting Git..."
    tar xzf git-$GIT_VERSION.tar.gz
fi

cd git-$GIT_VERSION

# Configure with curl for HTTPS
echo "Configuring Git with HTTPS support..."
make configure
./configure \
    --prefix=$PREFIX \
    --with-curl=/opt/local \
    --with-openssl=/opt/local \
    --with-expat=/opt/local \
    --without-tcltk \
    CC="gcc -arch ppc" \
    CFLAGS="-O2 -mcpu=7450 -I/opt/local/include" \
    LDFLAGS="-L/opt/local/lib"

echo "Building Git..."
make -j2 all

echo "Installing Git..."
sudo make install

echo ""
echo "=== Git $GIT_VERSION installed! ==="
echo ""
echo "Test with:"
echo "  $PREFIX/bin/git --version"
echo "  $PREFIX/bin/git ls-remote https://github.com/Scottcjn/pocketfox.git"
echo ""
echo "Configure Git:"
echo "  $PREFIX/bin/git config --global user.name 'Your Name'"
echo "  $PREFIX/bin/git config --global user.email 'you@example.com'"
echo ""
echo "Clone a repo:"
echo "  $PREFIX/bin/git clone https://github.com/Scottcjn/pocketfox.git"
echo ""
echo "Push with credentials:"
echo "  $PREFIX/bin/git push https://USER:TOKEN@github.com/USER/REPO.git"
echo ""
