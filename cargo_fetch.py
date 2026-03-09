#!/usr/bin/env python3
"""
cargo_fetch.py — Dependency Fetcher for rust-ppc-tiger
=====================================================

Fetches all crate dependencies from crates.io and git sources,
vendors them into a local directory, and generates a build manifest
for the PowerPC Tiger build system.

Parses Cargo.lock (v3/v4 format) for exact pinned versions.
Downloads .crate tarballs, verifies checksums, extracts source.

Part of the rust-ppc-tiger toolchain — Elyan Labs
Opus 4.6 + Sophia Elya
"""

import os
import sys
import re
import json
import hashlib
import tarfile
import shutil
import time
import subprocess
from pathlib import Path
from urllib.request import urlopen, Request
from urllib.error import HTTPError, URLError
from collections import defaultdict

# ── Constants ─────────────────────────────────────────────────

CRATES_IO_DL = "https://crates.io/api/v1/crates/{name}/{version}/download"
CRATES_IO_API = "https://crates.io/api/v1/crates/{name}/{version}"
USER_AGENT = "cargo_fetch/1.0.0 (rust-ppc-tiger; Elyan Labs; +https://github.com/Scottcjn/rust-ppc-tiger)"

# Rate limit: crates.io allows ~1 req/sec for unauthenticated
RATE_LIMIT_DELAY = 0.5

# Colors for terminal output
class C:
    BOLD = "\033[1m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    CYAN = "\033[36m"
    DIM = "\033[2m"
    RESET = "\033[0m"


# ── Cargo.lock Parser ─────────────────────────────────────────

class CrateDep:
    """A single resolved dependency from Cargo.lock."""
    __slots__ = ('name', 'version', 'source', 'checksum', 'dependencies')

    def __init__(self, name, version, source=None, checksum=None):
        self.name = name
        self.version = version
        self.source = source  # "registry+..." or "git+..."
        self.checksum = checksum
        self.dependencies = []

    @property
    def is_registry(self):
        return self.source and self.source.startswith("registry+")

    @property
    def is_git(self):
        return self.source and self.source.startswith("git+")

    @property
    def is_local(self):
        return self.source is None

    @property
    def git_url(self):
        if not self.is_git:
            return None
        # git+https://github.com/user/repo?branch=foo#commitsha
        url = self.source[4:]  # strip "git+"
        # Remove fragment (commit hash)
        url = url.split('#')[0]
        # Remove query params
        url = url.split('?')[0]
        return url

    @property
    def git_rev(self):
        if not self.is_git:
            return None
        if '#' in self.source:
            return self.source.split('#')[-1]
        return None

    @property
    def git_branch(self):
        if not self.is_git:
            return None
        if '?branch=' in self.source:
            branch = self.source.split('?branch=')[1]
            return branch.split('#')[0]
        return None

    def __repr__(self):
        return f"CrateDep({self.name} v{self.version})"


def parse_cargo_lock(lock_path):
    """Parse a Cargo.lock file (v3 or v4 format) into CrateDep objects."""
    with open(lock_path, 'r') as f:
        content = f.read()

    packages = []
    current = None

    for line in content.splitlines():
        line_stripped = line.strip()

        if line_stripped == '[[package]]':
            if current:
                packages.append(current)
            current = {'deps': []}
            continue

        if current is None:
            continue

        if line_stripped == '':
            continue

        # Parse key = "value"
        m = re.match(r'^(\w+)\s*=\s*"(.+)"$', line_stripped)
        if m:
            key, val = m.group(1), m.group(2)
            current[key] = val
            continue

        # Parse dependencies array
        if line_stripped == 'dependencies = [':
            continue
        if line_stripped == ']':
            continue

        # Dependency line: "crate_name version source"
        m = re.match(r'^\s*"(.+)",$', line_stripped)
        if not m:
            m = re.match(r'^\s*"(.+)"$', line_stripped)
        if m:
            dep_str = m.group(1)
            parts = dep_str.split(' ')
            current['deps'].append(parts[0])  # Just the crate name

    if current:
        packages.append(current)

    # Convert to CrateDep objects
    crates = []
    for pkg in packages:
        if 'name' not in pkg:
            continue
        dep = CrateDep(
            name=pkg['name'],
            version=pkg.get('version', '0.0.0'),
            source=pkg.get('source'),
            checksum=pkg.get('checksum'),
        )
        dep.dependencies = pkg.get('deps', [])
        crates.append(dep)

    return crates


# ── Crate Downloader ──────────────────────────────────────────

def download_crate(name, version, dest_dir, checksum=None):
    """Download a .crate tarball from crates.io and extract it."""
    crate_dir = os.path.join(dest_dir, f"{name}-{version}")

    # Skip if already fetched
    if os.path.isdir(crate_dir) and os.path.exists(os.path.join(crate_dir, '.fetched')):
        return crate_dir, 'cached'

    url = CRATES_IO_DL.format(name=name, version=version)
    tarball = os.path.join(dest_dir, f"{name}-{version}.crate")

    # Download
    req = Request(url, headers={'User-Agent': USER_AGENT})
    try:
        resp = urlopen(req, timeout=30)
        data = resp.read()
    except HTTPError as e:
        if e.code == 404:
            return None, f"not found on crates.io (404)"
        return None, f"HTTP {e.code}: {e.reason}"
    except URLError as e:
        return None, f"network error: {e.reason}"

    # Verify checksum if provided
    if checksum:
        computed = hashlib.sha256(data).hexdigest()
        if computed != checksum:
            return None, f"checksum mismatch! expected={checksum[:16]}... got={computed[:16]}..."

    # Write tarball
    with open(tarball, 'wb') as f:
        f.write(data)

    # Extract
    try:
        with tarfile.open(tarball, 'r:gz') as tar:
            # Security: check for path traversal
            for member in tar.getmembers():
                if member.name.startswith('/') or '..' in member.name:
                    return None, f"suspicious path in tarball: {member.name}"
            if sys.version_info >= (3, 12):
                tar.extractall(path=dest_dir, filter='data')
            else:
                tar.extractall(path=dest_dir)
    except tarfile.TarError as e:
        return None, f"extract failed: {e}"

    # Clean up tarball
    os.remove(tarball)

    # Mark as fetched
    with open(os.path.join(crate_dir, '.fetched'), 'w') as f:
        f.write(f"fetched_at={int(time.time())}\nchecksum={checksum or 'none'}\n")

    return crate_dir, 'downloaded'


def clone_git_dep(name, url, rev=None, branch=None, dest_dir='.'):
    """Clone a git dependency."""
    clone_dir = os.path.join(dest_dir, name)

    if os.path.isdir(clone_dir) and os.path.exists(os.path.join(clone_dir, '.fetched')):
        return clone_dir, 'cached'

    # Clone
    cmd = ['git', 'clone', '--depth', '1']
    if branch:
        cmd.extend(['--branch', branch])
    cmd.extend([url, clone_dir])

    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=60)
    except subprocess.CalledProcessError as e:
        return None, f"git clone failed: {e.stderr.decode()[:200]}"
    except subprocess.TimeoutExpired:
        return None, "git clone timed out"

    # Checkout specific revision if provided
    if rev:
        try:
            subprocess.run(
                ['git', '-C', clone_dir, 'fetch', '--depth', '1', 'origin', rev],
                check=True, capture_output=True, timeout=30
            )
            subprocess.run(
                ['git', '-C', clone_dir, 'checkout', rev],
                check=True, capture_output=True, timeout=10
            )
        except subprocess.CalledProcessError:
            pass  # Best effort — shallow clone may already be at the right commit

    # Mark fetched
    with open(os.path.join(clone_dir, '.fetched'), 'w') as f:
        f.write(f"fetched_at={int(time.time())}\nurl={url}\nrev={rev or 'HEAD'}\n")

    return clone_dir, 'cloned'


# ── Feature Resolution ────────────────────────────────────────

def parse_crate_features(crate_dir):
    """Read Cargo.toml from a fetched crate to understand its features."""
    toml_path = os.path.join(crate_dir, 'Cargo.toml')
    if not os.path.exists(toml_path):
        return {}, []

    features = {}
    deps = []
    section = ''

    with open(toml_path, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('['):
                section = line.strip('[]').strip()
                continue

            if section == 'features':
                m = re.match(r'^(\w[\w-]*)\s*=\s*\[(.+)\]', line)
                if m:
                    feat_name = m.group(1)
                    feat_deps = [s.strip().strip('"').strip("'") for s in m.group(2).split(',')]
                    features[feat_name] = feat_deps

            elif section in ('dependencies', 'dev-dependencies', 'build-dependencies'):
                m = re.match(r'^([\w-]+)\s*=', line)
                if m:
                    deps.append(m.group(1))

    return features, deps


# ── Build Manifest Generator ──────────────────────────────────

def generate_build_manifest(crates, vendor_dir, output_path):
    """Generate a JSON build manifest for the PPC build system."""
    manifest = {
        'version': 1,
        'generator': 'cargo_fetch.py (rust-ppc-tiger)',
        'generated_at': int(time.time()),
        'vendor_dir': vendor_dir,
        'crates': []
    }

    for crate in crates:
        crate_dir = os.path.join(vendor_dir, f"{crate.name}-{crate.version}")
        if not os.path.isdir(crate_dir):
            # Try git dep name
            crate_dir = os.path.join(vendor_dir, crate.name)

        # Find source files
        src_files = []
        src_dir = os.path.join(crate_dir, 'src')
        if os.path.isdir(src_dir):
            for root, dirs, files in os.walk(src_dir):
                for f in files:
                    if f.endswith('.rs'):
                        rel = os.path.relpath(os.path.join(root, f), crate_dir)
                        src_files.append(rel)

        # Detect if lib or bin
        has_lib = os.path.exists(os.path.join(crate_dir, 'src', 'lib.rs'))
        has_main = os.path.exists(os.path.join(crate_dir, 'src', 'main.rs'))

        entry = {
            'name': crate.name,
            'version': crate.version,
            'path': crate_dir,
            'source_type': 'registry' if crate.is_registry else 'git' if crate.is_git else 'local',
            'dependencies': crate.dependencies,
            'source_files': sorted(src_files),
            'source_count': len(src_files),
            'is_lib': has_lib,
            'is_bin': has_main,
        }
        manifest['crates'].append(entry)

    manifest['total_crates'] = len(manifest['crates'])
    manifest['total_source_files'] = sum(c['source_count'] for c in manifest['crates'])

    with open(output_path, 'w') as f:
        json.dump(manifest, f, indent=2)

    return manifest


# ── Dependency Graph (Topological Sort) ───────────────────────

def topo_sort(crates):
    """Topological sort of crates by dependencies."""
    by_name = {}
    for c in crates:
        key = c.name
        by_name[key] = c

    visited = set()
    order = []
    in_stack = set()

    def visit(name):
        if name in visited:
            return
        if name in in_stack:
            return  # Circular — skip silently
        in_stack.add(name)

        crate = by_name.get(name)
        if crate:
            for dep in crate.dependencies:
                visit(dep)

        in_stack.discard(name)
        visited.add(name)
        order.append(name)

    for c in crates:
        visit(c.name)

    return order


# ── Platform Filtering ────────────────────────────────────────

# Crates that are Windows/Linux-only and should be skipped on Darwin PPC
SKIP_CRATES = {
    # Windows-only
    'windows', 'windows-sys', 'windows-core', 'windows-targets',
    'windows-implement', 'windows-interface', 'windows-result',
    'windows_aarch64_gnullvm', 'windows_aarch64_msvc',
    'windows_i686_gnu', 'windows_i686_gnullvm', 'windows_i686_msvc',
    'windows_x86_64_gnu', 'windows_x86_64_gnullvm', 'windows_x86_64_msvc',
    'winres', 'embed-resource',
    # Wayland (Linux display server — not on Mac)
    'wayland-client', 'wayland-backend', 'wayland-sys', 'wayland-scanner',
    'wayland-protocols', 'wayland-protocols-wlr', 'wayland-protocols-plasma',
    'wayland-csd-frame', 'smithay-client-toolkit', 'sctk-adwaita',
    # X11 (Linux/Unix display — not on Tiger)
    'x11-dl', 'x11rb', 'x11rb-protocol', 'xkbcommon-dl',
    # Linux-specific
    'nix', 'linux-raw-sys',
    # Android
    'android-activity', 'android_system_properties', 'ndk', 'ndk-sys',
    # Web/WASM
    'web-sys', 'wasm-bindgen', 'wasm-bindgen-macro', 'wasm-bindgen-backend',
    'wasm-bindgen-shared', 'wasm-bindgen-futures', 'js-sys',
    # D-Bus / ATK (Linux accessibility)
    'accesskit_atspi_common', 'atspi', 'atspi-common', 'atspi-proxies',
    'zbus', 'zbus_macros', 'zbus_names', 'zvariant', 'zvariant_derive',
    'zvariant_utils',
}

# Crates that need special handling or patches for PPC Darwin
PATCH_CRATES = {
    'libc': 'Tiger libc definitions — may need PPC struct layouts',
    'ring': 'Crypto — needs PPC assembly stubs or fallback to OpenSSL',
    'mio': 'Async I/O — kqueue on Darwin, needs PPC-compatible atomics',
    'glutin': 'OpenGL context — needs AGL/CGL backend for Tiger',
    'winit': 'Window creation — needs Carbon/Cocoa backend for Tiger',
    'objc2': 'Objective-C bridge — Tiger has older runtime',
}


# ── Main ──────────────────────────────────────────────────────

def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='cargo_fetch — Dependency fetcher for rust-ppc-tiger',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s fetch /path/to/project           Fetch all dependencies
  %(prog)s fetch /path/to/project --no-gui  Skip GUI crates (egui, eframe, etc.)
  %(prog)s info /path/to/project            Show dependency summary
  %(prog)s list /path/to/project            List all crates
  %(prog)s graph /path/to/project           Show build order
        """
    )
    parser.add_argument('command', choices=['fetch', 'info', 'list', 'graph', 'clean'])
    parser.add_argument('project_dir', nargs='?', default='.')
    parser.add_argument('--vendor-dir', default=None, help='Vendor directory (default: <project>/vendor)')
    parser.add_argument('--no-gui', action='store_true', help='Skip GUI crates (egui, eframe, winit, etc.)')
    parser.add_argument('--skip-platform', action='store_true', help='Skip platform-incompatible crates')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be fetched without downloading')
    parser.add_argument('--manifest', default=None, help='Output build manifest path')
    parser.add_argument('--jobs', '-j', type=int, default=1, help='Parallel downloads (be nice to crates.io)')

    args = parser.parse_args()

    # Find Cargo.lock
    lock_path = os.path.join(args.project_dir, 'Cargo.lock')
    if not os.path.exists(lock_path):
        print(f"{C.RED}Error: No Cargo.lock found in {args.project_dir}{C.RESET}")
        print(f"Run 'cargo generate-lockfile' first, or provide a project with Cargo.lock")
        sys.exit(1)

    # Parse lock file
    print(f"{C.BOLD}; cargo_fetch — rust-ppc-tiger dependency fetcher{C.RESET}")
    print(f"; Parsing {lock_path}...")
    crates = parse_cargo_lock(lock_path)
    print(f"; Found {C.CYAN}{len(crates)}{C.RESET} packages in Cargo.lock")

    # Categorize
    registry_crates = [c for c in crates if c.is_registry]
    git_crates = [c for c in crates if c.is_git]
    local_crates = [c for c in crates if c.is_local]

    # GUI crates to skip
    GUI_SKIP = {
        'eframe', 'egui', 'egui_glow', 'egui-winit', 'egui_extras',
        'epaint', 'emath', 'ecolor',
        'glow', 'glutin', 'glutin-winit', 'glutin_egl_sys', 'glutin_glx_sys',
        'winit',
        'rfd', 'ashpd',
        'ab_glyph', 'ab_glyph_rasterizer', 'owned_ttf_parser', 'ttf-parser',
        'accesskit', 'accesskit_consumer', 'accesskit_macos', 'accesskit_unix',
        'accesskit_windows',
        'arboard', 'clipboard-win',
        'image', 'png', 'fdeflate', 'simd-adler32',
        'webbrowser',
        'calloop', 'calloop-wayland-source',
        'cursor-icon', 'dpi',
        'objc2-app-kit', 'objc2-quartz-core',
        'raw-window-handle',
        'softbuffer',
    }

    # ── INFO command ──────────────────────────────────────────

    if args.command == 'info':
        print(f"\n{C.BOLD}; Dependency Summary for {args.project_dir}{C.RESET}")
        print(f";")
        print(f";   Registry crates:  {C.GREEN}{len(registry_crates)}{C.RESET}")
        print(f";   Git dependencies: {C.YELLOW}{len(git_crates)}{C.RESET}")
        print(f";   Local crates:     {C.CYAN}{len(local_crates)}{C.RESET}")
        print(f";   Total:            {C.BOLD}{len(crates)}{C.RESET}")

        # Count skippable
        skip_count = sum(1 for c in crates if c.name in SKIP_CRATES)
        gui_count = sum(1 for c in crates if c.name in GUI_SKIP)
        core_count = len(crates) - skip_count - gui_count

        print(f";")
        print(f";   Platform-skip:    {C.DIM}{skip_count} (Windows/Linux/WASM){C.RESET}")
        print(f";   GUI-skip:         {C.DIM}{gui_count} (egui/winit/glutin){C.RESET}")
        print(f";   Core deps:        {C.GREEN}{core_count}{C.RESET} (needed for CLI build)")
        print(f";")

        # Show git deps
        if git_crates:
            print(f";   {C.YELLOW}Git dependencies:{C.RESET}")
            for c in git_crates:
                print(f";     {c.name} v{c.version} ← {c.git_url}")

        # Show crates needing patches
        patch_needed = [c for c in crates if c.name in PATCH_CRATES]
        if patch_needed:
            print(f";")
            print(f";   {C.YELLOW}Crates needing PPC patches:{C.RESET}")
            for c in patch_needed:
                print(f";     {c.name} v{c.version} — {PATCH_CRATES[c.name]}")

        return

    # ── LIST command ──────────────────────────────────────────

    if args.command == 'list':
        for c in sorted(crates, key=lambda x: x.name):
            skip = ''
            if c.name in SKIP_CRATES:
                skip = f' {C.DIM}[SKIP: platform]{C.RESET}'
            elif c.name in GUI_SKIP:
                skip = f' {C.DIM}[SKIP: gui]{C.RESET}'
            elif c.name in PATCH_CRATES:
                skip = f' {C.YELLOW}[PATCH]{C.RESET}'

            src = 'local' if c.is_local else 'git' if c.is_git else 'reg'
            print(f"  {src:5} {c.name:40} {c.version:12}{skip}")

        return

    # ── GRAPH command ─────────────────────────────────────────

    if args.command == 'graph':
        order = topo_sort(crates)
        print(f"\n{C.BOLD}; Build order ({len(order)} crates):{C.RESET}")
        for i, name in enumerate(order):
            crate = next((c for c in crates if c.name == name), None)
            if crate:
                skip = ''
                if name in SKIP_CRATES:
                    skip = f' {C.DIM}[SKIP]{C.RESET}'
                elif name in GUI_SKIP and args.no_gui:
                    skip = f' {C.DIM}[SKIP: gui]{C.RESET}'
                print(f"  {i+1:4}. {name:40} v{crate.version}{skip}")
        return

    # ── CLEAN command ─────────────────────────────────────────

    if args.command == 'clean':
        vendor_dir = args.vendor_dir or os.path.join(args.project_dir, 'vendor')
        if os.path.isdir(vendor_dir):
            print(f"; Removing {vendor_dir}...")
            shutil.rmtree(vendor_dir)
            print(f"; {C.GREEN}Clean!{C.RESET}")
        else:
            print(f"; Nothing to clean")
        return

    # ── FETCH command ─────────────────────────────────────────

    vendor_dir = args.vendor_dir or os.path.join(args.project_dir, 'vendor')
    os.makedirs(vendor_dir, exist_ok=True)

    # Filter crates
    to_fetch = []
    skipped = []
    for c in crates:
        if c.is_local:
            skipped.append((c, 'local'))
            continue
        if args.skip_platform and c.name in SKIP_CRATES:
            skipped.append((c, 'platform'))
            continue
        if args.no_gui and c.name in GUI_SKIP:
            skipped.append((c, 'gui'))
            continue
        to_fetch.append(c)

    print(f";")
    print(f"; Fetching {C.CYAN}{len(to_fetch)}{C.RESET} crates → {vendor_dir}/")
    print(f"; Skipping {C.DIM}{len(skipped)}{C.RESET} crates")
    print(f";")

    if args.dry_run:
        print(f"; {C.YELLOW}DRY RUN — no downloads{C.RESET}")
        for c in to_fetch:
            src = 'git' if c.is_git else 'crates.io'
            print(f";   WOULD FETCH: {c.name} v{c.version} ({src})")
        return

    # Fetch registry crates
    success = 0
    errors = []
    cached = 0

    for i, crate in enumerate(to_fetch):
        pct = int((i / len(to_fetch)) * 100)
        prefix = f"[{pct:3}%] ({i+1}/{len(to_fetch)})"

        if crate.is_registry:
            result_dir, status = download_crate(
                crate.name, crate.version, vendor_dir, crate.checksum
            )
            if result_dir:
                if status == 'cached':
                    cached += 1
                    print(f"  {prefix} {C.DIM}{crate.name} v{crate.version} (cached){C.RESET}")
                else:
                    success += 1
                    print(f"  {prefix} {C.GREEN}{crate.name} v{crate.version} ✓{C.RESET}")
                time.sleep(RATE_LIMIT_DELAY)
            else:
                errors.append((crate, status))
                print(f"  {prefix} {C.RED}{crate.name} v{crate.version} FAILED: {status}{C.RESET}")

        elif crate.is_git:
            result_dir, status = clone_git_dep(
                crate.name, crate.git_url,
                rev=crate.git_rev, branch=crate.git_branch,
                dest_dir=vendor_dir
            )
            if result_dir:
                if status == 'cached':
                    cached += 1
                    print(f"  {prefix} {C.DIM}{crate.name} (git, cached){C.RESET}")
                else:
                    success += 1
                    print(f"  {prefix} {C.GREEN}{crate.name} (git) ✓{C.RESET}")
            else:
                errors.append((crate, status))
                print(f"  {prefix} {C.RED}{crate.name} (git) FAILED: {status}{C.RESET}")

    # Summary
    print(f"\n{C.BOLD}; ═══════════════════════════════════════════════{C.RESET}")
    print(f"; {C.GREEN}Downloaded: {success}{C.RESET}")
    print(f"; {C.DIM}Cached:     {cached}{C.RESET}")
    print(f"; {C.DIM}Skipped:    {len(skipped)}{C.RESET}")
    if errors:
        print(f"; {C.RED}Errors:     {len(errors)}{C.RESET}")
        for crate, err in errors:
            print(f";   {C.RED}✗ {crate.name} v{crate.version}: {err}{C.RESET}")
    print(f"; {C.BOLD}; ═══════════════════════════════════════════════{C.RESET}")

    # Generate build manifest
    manifest_path = args.manifest or os.path.join(vendor_dir, 'build_manifest.json')
    print(f"\n; Generating build manifest → {manifest_path}")
    manifest = generate_build_manifest(crates, vendor_dir, manifest_path)
    print(f"; {manifest['total_crates']} crates, {manifest['total_source_files']} source files")
    print(f"\n; {C.GREEN}Done!{C.RESET} Use 'cargo_ppc build' with --vendor={vendor_dir}")


if __name__ == '__main__':
    main()
