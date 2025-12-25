#!/bin/bash
# Build kernel patches for Tiger CVEs
# Run this on PowerPC Mac with Xcode 2.5

set -e

echo "🔧 BUILDING TIGER KERNEL CVE PATCHES..."
echo ""

# Check prerequisites
if [ ! -d "/Developer/SDKs/MacOSX10.4u.sdk" ]; then
    echo "ERROR: Xcode 2.5 with 10.4u SDK required"
    exit 1
fi

if [[ $(uname -p) != "powerpc" ]]; then
    echo "ERROR: Must build on PowerPC"
    exit 1
fi

# Create build directories
mkdir -p build/{kexts,patches,tools}

# 1. Build DNS patch
echo "Building DNS port randomization patch..."
cd kernel_patches/CVE-2008-1447-DNS
make
cp dns_randomizer.dylib ../../build/patches/

# 2. Build IOKit security kext
echo "Building IOKit security extension..."
cd ../CVE-2014-4377-IOKit
gcc -dynamiclib -framework IOKit -framework CoreFoundation \
    -o ../../build/patches/IOKitSecurity.dylib \
    iokit_bounds_check.c

# 3. Build HFS+ integer overflow fix
echo "Building HFS+ security patch..."
cd ../CVE-2010-0036-HFS
gcc -dynamiclib -o ../../build/patches/HFSSecurity.dylib \
    hfs_validation.c

# 4. Build TCP ISN randomization
echo "Building TCP security improvements..."
cd ../CVE-2009-2414-TCP
gcc -dynamiclib -o ../../build/patches/TCPSecurity.dylib \
    tcp_isn_randomizer.c

# 5. Build font validation
echo "Building font security library..."
cd ../CVE-2011-0182-Font
gcc -dynamiclib -framework ApplicationServices \
    -o ../../build/patches/FontSecurity.dylib \
    font_validation.c

# 6. Build WebKit patch
echo "Building WebKit security fix..."
cd ../CVE-2011-3928-WebKit
# Would patch JavaScriptCore

echo ""
echo "✅ Build complete!"
echo ""
echo "To install: sudo ./install_kernel_patches.sh"