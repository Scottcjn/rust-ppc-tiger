#!/bin/bash
# Add kernel patches to existing DMG

echo "📦 Adding kernel patches to Tiger Security DMG..."

# Mount existing DMG
hdiutil attach ~/Desktop/TigerSecurity_VERIFIED.dmg -mountpoint /Volumes/TigerSecurityTemp

# Create new folder structure
mkdir -p TigerSecurityComplete
cp -R /Volumes/TigerSecurityTemp/* TigerSecurityComplete/
cp ~/Desktop/TigerKernelCVEs/TigerKernelCVEPatches.pkg TigerSecurityComplete/

# Update README
cat >> TigerSecurityComplete/README.txt << 'EOF'

KERNEL PATCHES (Optional)
=======================
TigerKernelCVEPatches.pkg contains additional kernel-level security fixes:
- CVE-2008-1447 (DNS poisoning)
- CVE-2009-2414 (TCP hijacking)
- CVE-2010-0036 (HFS+ overflow)
- CVE-2011-0182 (Font parsing)
- CVE-2014-4377 (IOKit privilege escalation)

Install separately after main security package.
Requires restart.
EOF

# Unmount old DMG
hdiutil detach /Volumes/TigerSecurityTemp

# Create new DMG with both packages
rm -f TigerSecurityComplete_WithKernel.dmg
hdiutil create -volname "Tiger Security Complete" -srcfolder TigerSecurityComplete -ov -format UDZO TigerSecurityComplete_WithKernel.dmg

# Cleanup
rm -rf TigerSecurityComplete

echo "✅ Created: TigerSecurityComplete_WithKernel.dmg"