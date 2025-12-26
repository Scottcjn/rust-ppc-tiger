#!/bin/bash
#
# Tiger Toolkit Installer
# "Ninite for PowerPC Mac OS X Tiger"
#
# One script to modernize your 2005 Mac
#
# https://github.com/Scottcjn/rust-ppc-tiger
#

set -e

VERSION="1.0.0"
INSTALL_DIR="/usr/local"
DOWNLOAD_BASE="https://github.com/Scottcjn/tiger-macports/raw/main/binaries"

# Colors (Tiger's bash supports these)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================
# BANNER
# ============================================================

show_banner() {
    echo ""
    echo "========================================================"
    echo "  Tiger Toolkit Installer v${VERSION}"
    echo "  Modern software for Mac OS X Tiger (10.4)"
    echo "========================================================"
    echo ""
    echo "  This will install:"
    echo "    - curl 7.88 with TLS 1.2"
    echo "    - OpenSSH 8.0p1 (secure SSH)"
    echo "    - wget with HTTPS"
    echo "    - Git with HTTPS push/pull"
    echo "    - Python 3.10"
    echo "    - rsync 3.x"
    echo "    - PocketFox browser"
    echo ""
    echo "  All binaries are pre-compiled for PowerPC."
    echo "  No Xcode or MacPorts required!"
    echo ""
    echo "========================================================"
    echo ""
}

# ============================================================
# DETECTION
# ============================================================

detect_system() {
    echo "[*] Detecting system..."

    # Check if we're on Mac OS X
    if [ "$(uname)" != "Darwin" ]; then
        echo "${RED}ERROR: This script is for Mac OS X only${NC}"
        exit 1
    fi

    # Get OS version
    OS_VERSION=$(sw_vers -productVersion 2>/dev/null || echo "unknown")
    echo "    OS Version: $OS_VERSION"

    # Check for Tiger (10.4.x)
    case "$OS_VERSION" in
        10.4*)
            echo "    ${GREEN}Mac OS X Tiger detected${NC}"
            ;;
        10.5*)
            echo "    ${YELLOW}Mac OS X Leopard detected - should work${NC}"
            ;;
        *)
            echo "    ${YELLOW}Warning: Untested OS version${NC}"
            ;;
    esac

    # Detect CPU
    CPU_TYPE=$(machine 2>/dev/null || uname -p)
    echo "    CPU: $CPU_TYPE"

    case "$CPU_TYPE" in
        ppc750|ppc7400|ppc7450|ppc970|powerpc|ppc)
            echo "    ${GREEN}PowerPC processor detected${NC}"
            ;;
        *)
            echo "    ${RED}ERROR: This toolkit is for PowerPC Macs only${NC}"
            exit 1
            ;;
    esac

    # Detect specific model
    if sysctl -n hw.model 2>/dev/null | grep -qi "PowerMac"; then
        MODEL=$(sysctl -n hw.model)
        echo "    Model: $MODEL"

        # G5 detection
        if echo "$MODEL" | grep -qE "PowerMac(7|9|11)"; then
            CPU_GEN="G5"
        else
            CPU_GEN="G4"
        fi
    elif sysctl -n hw.model 2>/dev/null | grep -qi "PowerBook"; then
        MODEL=$(sysctl -n hw.model)
        echo "    Model: $MODEL (PowerBook)"
        CPU_GEN="G4"
    elif sysctl -n hw.model 2>/dev/null | grep -qi "iBook"; then
        MODEL=$(sysctl -n hw.model)
        echo "    Model: $MODEL (iBook)"
        CPU_GEN="G4"
    else
        CPU_GEN="G4"  # Default assumption
    fi

    echo "    Generation: $CPU_GEN"

    # Check available disk space
    DISK_FREE=$(df -k / | tail -1 | awk '{print $4}')
    DISK_FREE_MB=$((DISK_FREE / 1024))
    echo "    Free disk: ${DISK_FREE_MB}MB"

    if [ "$DISK_FREE_MB" -lt 500 ]; then
        echo "    ${RED}Warning: Low disk space. Need at least 500MB.${NC}"
    fi

    echo ""
}

# ============================================================
# DOWNLOAD HELPER
# ============================================================

download_file() {
    local URL="$1"
    local OUTPUT="$2"

    # Try curl first (might have old version)
    if command -v /opt/local/bin/curl >/dev/null 2>&1; then
        /opt/local/bin/curl -L -o "$OUTPUT" "$URL"
    elif command -v /usr/local/bin/curl >/dev/null 2>&1; then
        /usr/local/bin/curl -L -o "$OUTPUT" "$URL"
    elif command -v curl >/dev/null 2>&1; then
        # System curl - may not support modern TLS
        curl -L -o "$OUTPUT" "$URL" 2>/dev/null || {
            echo "${YELLOW}System curl failed (old TLS). Trying alternate...${NC}"
            return 1
        }
    else
        echo "${RED}No download tool available${NC}"
        return 1
    fi
}

# ============================================================
# PACKAGE INSTALLERS
# ============================================================

install_curl() {
    echo "[*] Installing curl 7.88 with TLS 1.2..."

    if [ -x /usr/local/bin/curl ]; then
        EXISTING=$(/usr/local/bin/curl --version 2>/dev/null | head -1 || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping curl"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/curl-7.88.1-tiger-ppc.tar.gz" "curl.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/curl.tar.gz
    rm /tmp/curl.tar.gz

    echo "    ${GREEN}curl installed: $(/usr/local/bin/curl --version | head -1)${NC}"
}

install_openssh() {
    echo "[*] Installing OpenSSH 8.0p1..."

    if [ -x /usr/local/bin/ssh ]; then
        EXISTING=$(/usr/local/bin/ssh -V 2>&1 || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping OpenSSH"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/openssh-8.0p1-tiger-ppc.tar.gz" "openssh.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/openssh.tar.gz
    rm /tmp/openssh.tar.gz

    echo "    ${GREEN}OpenSSH installed: $(/usr/local/bin/ssh -V 2>&1)${NC}"
}

install_wget() {
    echo "[*] Installing wget with TLS..."

    if [ -x /usr/local/bin/wget ]; then
        EXISTING=$(/usr/local/bin/wget --version 2>/dev/null | head -1 || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping wget"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/wget-tiger-ppc.tar.gz" "wget.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/wget.tar.gz
    rm /tmp/wget.tar.gz

    echo "    ${GREEN}wget installed${NC}"
}

install_git() {
    echo "[*] Installing Git with HTTPS..."

    if [ -x /usr/local/bin/git ]; then
        EXISTING=$(/usr/local/bin/git --version 2>/dev/null || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping Git"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/git-tiger-ppc.tar.gz" "git.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/git.tar.gz
    rm /tmp/git.tar.gz

    echo "    ${GREEN}Git installed: $(/usr/local/bin/git --version)${NC}"
}

install_python() {
    echo "[*] Installing Python 3.10..."

    if [ -x /usr/local/bin/python3.10 ]; then
        EXISTING=$(/usr/local/bin/python3.10 --version 2>/dev/null || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping Python"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/python310-tiger-ppc.tar.gz" "python.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/python.tar.gz
    rm /tmp/python.tar.gz

    # Create symlinks
    sudo ln -sf /usr/local/bin/python3.10 /usr/local/bin/python3
    sudo ln -sf /usr/local/bin/pip3.10 /usr/local/bin/pip3

    echo "    ${GREEN}Python installed: $(/usr/local/bin/python3.10 --version)${NC}"
}

install_rsync() {
    echo "[*] Installing rsync 3.x..."

    if [ -x /usr/local/bin/rsync ]; then
        EXISTING=$(/usr/local/bin/rsync --version 2>/dev/null | head -1 || echo "unknown")
        echo "    Existing: $EXISTING"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping rsync"
            return 0
        fi
    fi

    cd /tmp
    download_file "${DOWNLOAD_BASE}/rsync-tiger-ppc.tar.gz" "rsync.tar.gz" || {
        echo "    ${RED}Download failed${NC}"
        return 1
    }

    cd /usr/local
    sudo tar xzf /tmp/rsync.tar.gz
    rm /tmp/rsync.tar.gz

    echo "    ${GREEN}rsync installed${NC}"
}

install_pocketfox() {
    echo "[*] Installing PocketFox browser..."

    if [ -d /Applications/PocketFox.app ]; then
        echo "    PocketFox already installed"
        read -p "    Reinstall? [y/N] " REPLY
        if [ "$REPLY" != "y" ] && [ "$REPLY" != "Y" ]; then
            echo "    Skipping PocketFox"
            return 0
        fi
    fi

    cd /tmp
    download_file "https://github.com/Scottcjn/pocketfox/releases/latest/download/PocketFox-Tiger.dmg" "pocketfox.dmg" || {
        echo "    ${YELLOW}Download failed - PocketFox is optional${NC}"
        return 0
    }

    # Mount and copy
    hdiutil attach /tmp/pocketfox.dmg -nobrowse -quiet
    sudo cp -R /Volumes/PocketFox/PocketFox.app /Applications/
    hdiutil detach /Volumes/PocketFox -quiet
    rm /tmp/pocketfox.dmg

    echo "    ${GREEN}PocketFox installed in /Applications${NC}"
}

# ============================================================
# PATH SETUP
# ============================================================

setup_path() {
    echo "[*] Setting up PATH..."

    # Add to .profile if not already there
    PROFILE="$HOME/.profile"

    if ! grep -q "/usr/local/bin" "$PROFILE" 2>/dev/null; then
        echo "" >> "$PROFILE"
        echo "# Tiger Toolkit - added by installer" >> "$PROFILE"
        echo 'export PATH="/usr/local/bin:$PATH"' >> "$PROFILE"
        echo "    Added /usr/local/bin to PATH in $PROFILE"
    else
        echo "    PATH already configured"
    fi

    # Also add to .bashrc for bash users
    if [ -f "$HOME/.bashrc" ]; then
        if ! grep -q "/usr/local/bin" "$HOME/.bashrc" 2>/dev/null; then
            echo 'export PATH="/usr/local/bin:$PATH"' >> "$HOME/.bashrc"
        fi
    fi

    echo "    ${GREEN}PATH configured. Restart Terminal to apply.${NC}"
}

# ============================================================
# VERIFICATION
# ============================================================

verify_install() {
    echo ""
    echo "========================================================"
    echo "  Installation Summary"
    echo "========================================================"
    echo ""

    # Check each tool
    for tool in curl ssh wget git python3 rsync; do
        if [ -x "/usr/local/bin/$tool" ]; then
            VERSION=$("/usr/local/bin/$tool" --version 2>&1 | head -1)
            echo "  ${GREEN}[OK]${NC} $tool: $VERSION"
        else
            echo "  ${YELLOW}[--]${NC} $tool: not installed"
        fi
    done

    # Check PocketFox
    if [ -d /Applications/PocketFox.app ]; then
        echo "  ${GREEN}[OK]${NC} PocketFox: installed"
    else
        echo "  ${YELLOW}[--]${NC} PocketFox: not installed"
    fi

    echo ""
    echo "========================================================"
    echo "  Quick Test Commands"
    echo "========================================================"
    echo ""
    echo "  # Test HTTPS:"
    echo "  /usr/local/bin/curl https://github.com"
    echo ""
    echo "  # Test Git clone:"
    echo "  /usr/local/bin/git clone https://github.com/Scottcjn/pocketfox.git"
    echo ""
    echo "  # Test Python:"
    echo "  /usr/local/bin/python3 -c \"import ssl; print(ssl.OPENSSL_VERSION)\""
    echo ""
    echo "========================================================"
    echo ""
}

# ============================================================
# INTERACTIVE MENU
# ============================================================

interactive_menu() {
    echo "Select packages to install:"
    echo ""
    echo "  [1] curl 7.88 (TLS 1.2 for HTTPS)"
    echo "  [2] OpenSSH 8.0p1 (secure SSH)"
    echo "  [3] wget (HTTPS downloads)"
    echo "  [4] Git (HTTPS push/pull)"
    echo "  [5] Python 3.10"
    echo "  [6] rsync 3.x"
    echo "  [7] PocketFox browser"
    echo ""
    echo "  [A] Install ALL (recommended)"
    echo "  [Q] Quit"
    echo ""
    read -p "Enter choice (1-7, A, or Q): " CHOICE

    case "$CHOICE" in
        1) install_curl ;;
        2) install_openssh ;;
        3) install_wget ;;
        4) install_git ;;
        5) install_python ;;
        6) install_rsync ;;
        7) install_pocketfox ;;
        [Aa])
            install_curl
            install_openssh
            install_wget
            install_git
            install_python
            install_rsync
            install_pocketfox
            ;;
        [Qq])
            echo "Goodbye!"
            exit 0
            ;;
        *)
            echo "Invalid choice"
            interactive_menu
            ;;
    esac
}

# ============================================================
# MAIN
# ============================================================

main() {
    # Check for root
    if [ "$EUID" -ne 0 ] && [ "$USER" != "root" ]; then
        echo "Note: Some operations require sudo. You may be prompted for password."
    fi

    show_banner
    detect_system

    # Parse arguments
    case "${1:-}" in
        --all)
            install_curl
            install_openssh
            install_wget
            install_git
            install_python
            install_rsync
            install_pocketfox
            ;;
        --curl) install_curl ;;
        --ssh) install_openssh ;;
        --wget) install_wget ;;
        --git) install_git ;;
        --python) install_python ;;
        --rsync) install_rsync ;;
        --pocketfox) install_pocketfox ;;
        --help|-h)
            echo "Usage: $0 [option]"
            echo ""
            echo "Options:"
            echo "  --all       Install everything"
            echo "  --curl      Install curl only"
            echo "  --ssh       Install OpenSSH only"
            echo "  --wget      Install wget only"
            echo "  --git       Install Git only"
            echo "  --python    Install Python only"
            echo "  --rsync     Install rsync only"
            echo "  --pocketfox Install PocketFox only"
            echo ""
            echo "Run without options for interactive menu."
            exit 0
            ;;
        *)
            interactive_menu
            ;;
    esac

    setup_path
    verify_install

    echo "Tiger Toolkit installation complete!"
    echo ""
    echo "Join our Discord: https://discord.gg/tQ4q3z4M"
    echo ""
}

main "$@"
