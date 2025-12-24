#!/bin/bash
#
# Cargo wrapper for PowerPC Tiger
# Routes Rust compilation through rustc_ppc
#
# Opus 4.5
#

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUSTC_PPC="$SCRIPT_DIR/rustc_ppc"
BUILD_SYSTEM="$SCRIPT_DIR/rustc_build_system"

# Ensure our tools are built
if [ ! -x "$RUSTC_PPC" ]; then
    echo "Building rustc_ppc..."
    gcc -O3 -o "$RUSTC_PPC" "$SCRIPT_DIR/rustc_100_percent.c" 2>/dev/null || \
    gcc -O2 -o "$RUSTC_PPC" "$SCRIPT_DIR/rustc_100_percent.c"
fi

if [ ! -x "$BUILD_SYSTEM" ]; then
    echo "Building build system..."
    gcc -O2 -o "$BUILD_SYSTEM" "$SCRIPT_DIR/rustc_build_system.c"
fi

# Parse cargo-like commands
case "$1" in
    build)
        shift
        echo "; cargo_ppc build"

        # Find project root (look for Cargo.toml)
        PROJECT_DIR="."
        while [ ! -f "$PROJECT_DIR/Cargo.toml" ] && [ "$PROJECT_DIR" != "/" ]; do
            PROJECT_DIR="$(dirname "$PROJECT_DIR")"
        done

        if [ ! -f "$PROJECT_DIR/Cargo.toml" ]; then
            echo "Error: No Cargo.toml found"
            exit 1
        fi

        # Use our build system
        "$BUILD_SYSTEM" build "$PROJECT_DIR" "$@"
        ;;

    check)
        shift
        echo "; cargo_ppc check (syntax only)"
        # Just parse, don't compile
        for src in $(find src -name "*.rs" 2>/dev/null); do
            echo ";   Checking $src..."
            "$RUSTC_PPC" --check "$src" 2>&1 || true
        done
        ;;

    run)
        shift
        echo "; cargo_ppc run"
        "$0" build "$@"
        # Find and run the binary
        BIN=$(find target -type f -perm +111 -name "$(basename $(pwd))" 2>/dev/null | head -1)
        if [ -x "$BIN" ]; then
            "$BIN"
        else
            echo "Error: Binary not found"
        fi
        ;;

    test)
        shift
        echo "; cargo_ppc test"
        echo "; (Test framework for PowerPC TBD)"
        ;;

    clean)
        echo "; cargo_ppc clean"
        rm -rf target/
        ;;

    new)
        NAME="$2"
        if [ -z "$NAME" ]; then
            echo "Usage: cargo_ppc new <project-name>"
            exit 1
        fi

        mkdir -p "$NAME/src"

        cat > "$NAME/Cargo.toml" << EOF
[package]
name = "$NAME"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "$NAME"
path = "src/main.rs"

[dependencies]
EOF

        cat > "$NAME/src/main.rs" << 'EOF'
fn main() {
    println!("Hello from PowerPC Tiger!");
}
EOF

        echo "Created project $NAME"
        ;;

    rustc)
        # Direct rustc passthrough
        shift
        "$RUSTC_PPC" "$@"
        ;;

    version|--version|-V)
        echo "cargo_ppc 1.0.0 (PowerPC Tiger)"
        echo "rustc_ppc 1.0.0 (Opus 4.5)"
        echo "Target: powerpc-apple-darwin8"
        ;;

    help|--help|-h)
        echo "cargo_ppc - Cargo for PowerPC Mac OS X Tiger"
        echo ""
        echo "Usage: cargo_ppc <command> [options]"
        echo ""
        echo "Commands:"
        echo "  build     Compile the current package"
        echo "  check     Check syntax without compiling"
        echo "  run       Build and run"
        echo "  test      Run tests"
        echo "  clean     Remove target directory"
        echo "  new       Create a new project"
        echo "  rustc     Direct rustc_ppc invocation"
        echo "  version   Show version info"
        echo ""
        echo "Target: powerpc-apple-darwin8 (Tiger/Leopard)"
        echo "CPU: G4 (7450) or G5 (970)"
        echo "SIMD: AltiVec enabled"
        ;;

    *)
        echo "Unknown command: $1"
        echo "Run 'cargo_ppc help' for usage"
        exit 1
        ;;
esac
