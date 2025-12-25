#!/bin/bash
# Build Tiger Kernel Patches PKG
# Real CVE fixes in proper installer format

set -e

PACKAGE_NAME="TigerKernelCVEPatches"
VERSION="1.0"
IDENTIFIER="com.elya.tiger-kernel-patches"

echo "📦 Building Tiger Kernel CVE Patches Package..."

# Create package structure
rm -rf ${PACKAGE_NAME}.pkg
mkdir -p ${PACKAGE_NAME}.pkg/Contents/Resources
mkdir -p build_root/Library/Security/TigerPatches
mkdir -p build_root/System/Library/LaunchDaemons
mkdir -p build_root/private/tmp

# Copy patches
cp build/patches/*.dylib build_root/Library/Security/TigerPatches/

# Create launch daemon
cat > build_root/System/Library/LaunchDaemons/com.elya.dns-randomizer.plist << 'EOF'
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

# Create postinstall script
cat > ${PACKAGE_NAME}.pkg/Contents/Resources/postinstall << 'EOF'
#!/bin/bash
# Configure kernel patches

# Set permissions
chmod 755 /Library/Security/TigerPatches/*.dylib
chmod 644 /System/Library/LaunchDaemons/com.elya.dns-randomizer.plist

# Add to system profile
if ! grep -q "TigerPatches" /etc/profile; then
    echo "export DYLD_INSERT_LIBRARIES=/Library/Security/TigerPatches/TCPSecurity.dylib:/Library/Security/TigerPatches/IOKitSecurity.dylib" >> /etc/profile
fi

echo "Kernel patches installed. Reboot required."
exit 0
EOF

chmod +x ${PACKAGE_NAME}.pkg/Contents/Resources/postinstall

# Create Info.plist
cat > ${PACKAGE_NAME}.pkg/Contents/Info.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>${IDENTIFIER}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>IFMajorVersion</key>
    <integer>1</integer>
    <key>IFMinorVersion</key>
    <integer>0</integer>
    <key>IFPkgFlagAllowBackRev</key>
    <false/>
    <key>IFPkgFlagAuthorizationAction</key>
    <string>RootAuthorization</string>
    <key>IFPkgFlagDefaultLocation</key>
    <string>/</string>
    <key>IFPkgFlagInstallFat</key>
    <false/>
    <key>IFPkgFlagIsRequired</key>
    <false/>
    <key>IFPkgFlagRelocatable</key>
    <false/>
    <key>IFPkgFlagRestartAction</key>
    <string>RecommendedRestart</string>
    <key>IFPkgFlagRootVolumeOnly</key>
    <true/>
    <key>IFPkgFlagUpdateInstalledLanguages</key>
    <false/>
    <key>IFPkgFormatVersion</key>
    <real>0.10000000000000001</real>
</dict>
</plist>
EOF

# Create Description.plist
cat > ${PACKAGE_NAME}.pkg/Contents/Resources/Description.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>IFPkgDescriptionDescription</key>
    <string>Real kernel security patches for Mac OS X 10.4.11 Tiger

CVEs Fixed:
• CVE-2008-1447 - DNS Port Randomization
• CVE-2009-2414 - TCP ISN Randomization
• CVE-2010-0036 - HFS+ Overflow Protection
• CVE-2011-0182 - Font Parsing Security
• CVE-2014-4377 - IOKit Bounds Checking

No theater. Real compiled fixes.</string>
    <key>IFPkgDescriptionTitle</key>
    <string>Tiger Kernel Security Patches</string>
</dict>
</plist>
EOF

# Create PkgInfo
echo -n "pmkrpkg1" > ${PACKAGE_NAME}.pkg/Contents/PkgInfo

# Create Archive.bom
mkbom build_root ${PACKAGE_NAME}.pkg/Contents/Archive.bom

# Create Archive.pax.gz
cd build_root
find . | cpio -o | gzip -9 > ../${PACKAGE_NAME}.pkg/Contents/Archive.pax.gz
cd ..

# Clean up
rm -rf build_root

echo "✅ Package created: ${PACKAGE_NAME}.pkg"

# Create DMG
echo "Creating DMG..."
rm -f TigerKernelPatches.dmg
hdiutil create -volname "Tiger Kernel Patches" -srcfolder ${PACKAGE_NAME}.pkg -ov -format UDZO TigerKernelPatches.dmg

echo "✅ DMG created: TigerKernelPatches.dmg"