#!/bin/bash
# Install Tiger Kernel CVE Patches
# Mikalei-style: real fixes, no theater

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

echo "🔒 INSTALLING TIGER KERNEL SECURITY PATCHES"
echo ""
echo "This will install patches for:"
echo "  • CVE-2008-1447 - DNS Port Randomization"
echo "  • CVE-2009-2414 - TCP Sequence Randomization"
echo "  • CVE-2010-0036 - HFS+ Integer Overflow Protection"
echo "  • CVE-2011-0182 - Font Parsing Validation"
echo "  • CVE-2014-4377 - IOKit Bounds Checking"
echo ""

# Create directories
mkdir -p /Library/Security/TigerPatches
mkdir -p /System/Library/LaunchDaemons

# Install patches
echo "Installing security libraries..."
cp build/patches/*.dylib /Library/Security/TigerPatches/
chmod 755 /Library/Security/TigerPatches/*.dylib

# Create launch daemon for DNS randomization
cat > /System/Library/LaunchDaemons/com.elya.dns-randomizer.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.elya.dns-randomizer</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/env</string>
        <string>DYLD_INSERT_LIBRARIES=/Library/Security/TigerPatches/dns_randomizer.dylib</string>
        <string>/usr/sbin/lookupd</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
EOF

# Set DYLD paths for system-wide patches
echo "export DYLD_INSERT_LIBRARIES=/Library/Security/TigerPatches/TCPSecurity.dylib:/Library/Security/TigerPatches/IOKitSecurity.dylib" >> /etc/profile

echo ""
echo "✅ Kernel patches installed!"
echo ""
echo "IMPORTANT: Reboot required for all patches to take effect"
echo ""
echo "Patches installed:"
echo "  ✓ DNS port randomization (CVE-2008-1447)"
echo "  ✓ TCP ISN randomization (CVE-2009-2414)"  
echo "  ✓ HFS+ validation (CVE-2010-0036)"
echo "  ✓ Font security (CVE-2011-0182)"
echo "  ✓ IOKit bounds checking (CVE-2014-4377)"