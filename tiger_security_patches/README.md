# Tiger Kernel CVE Patches

Kernel-level security fixes for Mac OS X 10.4.11/10.4.12 Tiger PowerPC

## WARNING - EXPERIMENTAL

**These patches are EXPERIMENTAL and have NOT been tested on all PowerPC Macs.**

- Use at your own risk
- Make a FULL BACKUP before installing
- Test on non-production machines first
- Some patches may cause kernel panics on certain hardware
- The AirPort fix was removed due to causing system instability on G4 iMac

**DO NOT INSTALL ON:**
- Your only Mac
- Systems with irreplaceable data
- Production servers

## CVEs Addressed

### System/Kernel CVEs:
- [x] CVE-2008-1447 - DNS cache poisoning (Kaminsky attack)
- [x] CVE-2014-4377 - IOKit privilege escalation
- [x] CVE-2010-0036 - HFS+ integer overflow
- [x] CVE-2009-2414 - TCP/IP connection hijacking
- [x] CVE-2011-0182 - Font parsing RCE

### Implementation Method:
- DYLD_INSERT_LIBRARIES injection for userspace patches
- Kernel extensions (kexts) for kernel-level patches
- LaunchDaemons for persistent activation

## Project Structure

```
tiger_security_patches/
├── docs/           # CVE analysis and patch documentation
├── kernel_patches/ # Kernel source patches (C source)
│   ├── CVE-2008-1447-DNS/    # DNS port randomization
│   ├── CVE-2009-2414-TCP/    # TCP ISN randomization
│   ├── CVE-2010-0036-HFS/    # HFS+ overflow protection
│   ├── CVE-2011-0182-Font/   # Font parsing security
│   └── CVE-2014-4377-IOKit/  # IOKit bounds checking
├── kexts/         # Prebuilt kernel extensions
├── build/         # Build artifacts
├── tools/         # Build and analysis tools
├── scripts/       # Installation scripts
└── *.dmg          # Installer disk images
```

## Requirements

- Mac OS X 10.4.11 or 10.4.12 Tiger on PowerPC
- Xcode 2.5 with Kernel Development Kit (for building from source)
- Darwin 8.x kernel sources (for kext development)
- Root access for installation

## Build Instructions

```bash
# Build the installer package
./BUILD_KERNEL_PKG.sh

# Creates:
# - TigerKernelCVEPatches.pkg
# - TigerKernelPatches.dmg
```

## Installation

1. Mount the DMG
2. Run the PKG installer
3. Enter administrator password
4. **REBOOT REQUIRED**

## How It Works

### DNS Port Randomization (CVE-2008-1447)
```c
// Hooks bind() to randomize source ports for DNS queries
// Prevents Kaminsky DNS cache poisoning attack
sin->sin_port = htons(1024 + (rand() % 64511));
```

### IOKit Bounds Checking (CVE-2014-4377)
```c
// Validates IOBluetoothHCIUserClient arguments
if (args->structureInputSize > MAX_STRUCT_INPUT_SIZE) {
    return kIOReturnBadArgument;
}
```

## Tested Hardware

| Model | Status | Notes |
|-------|--------|-------|
| Power Mac G4 Dual 1.25 | Tested | OK |
| Power Mac G5 Quad | Untested | Should work |
| iBook G4 | Untested | Unknown |
| PowerBook G4 | Untested | Unknown |
| iMac G4 | Partial | AirPort fix causes issues |
| iMac G5 | Untested | Unknown |

## Known Issues

1. **AirPort Instability**: The original AirPort security patch caused kernel panics on G4 iMac. This patch has been REMOVED.

2. **Bluetooth Kext**: The IOKit security filter may interfere with Bluetooth on some systems.

3. **Boot Delays**: The DYLD_INSERT_LIBRARIES method adds ~1-2 seconds to app launch time.

## Uninstallation

```bash
# Remove patches
sudo rm -rf /Library/Security/TigerPatches/
sudo rm /System/Library/LaunchDaemons/com.elya.dns-randomizer.plist

# Remove from profile
sudo sed -i '' '/TigerPatches/d' /etc/profile

# Reboot
sudo reboot
```

## Credits

- **Scott (Scottcjn)** - Creator, architect, hardware lab, testing
- **Claude (Opus 4.1/4.5)** - Implementation assistance

*Designed by Scott, coded with Claude*

## License

MIT License - Use at your own risk.

---

*"Keeping 20-year-old Macs secure, one CVE at a time."*

**Remember: BACKUP FIRST, INSTALL SECOND!**
