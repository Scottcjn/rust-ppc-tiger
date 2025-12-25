# Tiger Kernel CVE Patches

Real kernel-level security fixes for Mac OS X 10.4.11 Tiger.
No theater. Just actual compiled patches.

## CVEs Fixed

1. **CVE-2008-1447** - DNS Cache Poisoning
   - Implements source port randomization for DNS queries
   - Prevents Kaminsky attack

2. **CVE-2009-2414** - TCP Sequence Number Prediction
   - Randomizes TCP Initial Sequence Numbers (ISN)
   - Prevents connection hijacking

3. **CVE-2010-0036** - HFS+ Integer Overflow
   - Validates extent calculations
   - Prevents filesystem corruption attacks

4. **CVE-2011-0182** - Font Parsing RCE
   - Validates font table boundaries
   - Prevents malicious font exploitation

5. **CVE-2014-4377** - IOKit Privilege Escalation
   - Adds bounds checking to IODataQueue
   - Prevents kernel memory corruption

## Installation

```bash
cd ~/Desktop/TigerKernelCVEs
sudo ./scripts/install_kernel_patches.sh
# Reboot required
```

## Technical Details

All patches compiled on PowerPC with gcc 4.0.1.
Tested on Mac OS X 10.4.11 build 8S165.

Libraries installed to: `/Library/Security/TigerPatches/`

---
Built by Elya for real Tiger security.
Like Mikalei would have done.