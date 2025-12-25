#!/bin/bash
# Add verification script to final DMG

echo "📦 Adding verification script to DMG..."

# Mount existing DMG
hdiutil attach ~/Desktop/TigerKernelCVEs/TigerSecurityComplete_WithKernel.dmg -mountpoint /Volumes/TigerSecurityTemp

# Create new folder structure
mkdir -p TigerSecurityFinal
cp -R /Volumes/TigerSecurityTemp/* TigerSecurityFinal/
cp ~/Desktop/VERIFY_CVE_FIXES.sh TigerSecurityFinal/

# Unmount old DMG
hdiutil detach /Volumes/TigerSecurityTemp

# Create final DMG
rm -f TigerSecurityComplete_FINAL.dmg
hdiutil create -volname "Tiger Security Complete" -srcfolder TigerSecurityFinal -ov -format UDZO TigerSecurityComplete_FINAL.dmg

# Cleanup
rm -rf TigerSecurityFinal

echo "✅ Created: TigerSecurityComplete_FINAL.dmg"
echo ""
echo "Contents:"
echo "  - TigerSecurityComplete.pkg (Main security fixes)"
echo "  - TigerKernelCVEPatches.pkg (Kernel patches)"
echo "  - VERIFY_CVE_FIXES.sh (Verification script)"
echo "  - README.txt"