// Minimal Rust test for PowerPC Tiger
// Compiles and runs on real G4 hardware!
//
// Compile: ./rustc_ppc minimal.rs > minimal.s
// Assemble: as -o minimal.o minimal.s
// Link: gcc -o minimal minimal.o
// Run: ./minimal (exits with code 0)

fn main() {
    let x = 100;
}
