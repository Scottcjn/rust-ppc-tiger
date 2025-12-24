# Rust Compiler for PowerPC Mac OS X Tiger

**A Rust-to-PowerPC compiler targeting Mac OS X Tiger (10.4) and Leopard (10.5)**

This is a custom Rust compiler implementation that generates native PowerPC assembly with AltiVec SIMD support.

## What's Included

### Core Compiler
| File | Description | Progress |
|------|-------------|----------|
| `rustc_100_percent.c` | Full compiler implementation | 100% |
| `rustc_75_percent.c` | Major features implementation | 75% |
| `rustc_ppc.c` | Basic compiler core | Base |
| `rustc_ppc_advanced.c` | Advanced features | Extension |
| `rustc_ppc_final.c` | Finalized implementation | Complete |
| `rustc_ppc_modern.c` | Modern Rust syntax support | Extension |
| `mini_rustc.c` | Minimal bootstrap compiler | Bootstrap |

### Language Feature Support
| File | Feature |
|------|---------|
| `rustc_closure_support.c` | Closures and lambdas |
| `rustc_closure_fixed.c` | Closure bug fixes |
| `rustc_generics_simple.c` | Generic types |
| `rustc_trait_support.c` | Traits and impl blocks |
| `rustc_reference_support.c` | References and borrowing |
| `rustc_module_support.c` | Module system |

### Code Generation
| File | Description |
|------|-------------|
| `rustc_altivec_codegen.c` | AltiVec SIMD code generation |
| `rustc_modern_simple.c` | Modern codegen pipeline |

## Target Platform

- **OS**: Mac OS X Tiger (10.4) / Leopard (10.5)
- **CPU**: PowerPC G4 (7450/7447) / G5 (970)
- **SIMD**: AltiVec/Velocity Engine acceleration

## Building the Compiler

```bash
# On Tiger/Leopard with Xcode
gcc -O3 -mcpu=7450 -maltivec -o rustc_ppc rustc_100_percent.c

# For G5
gcc -O3 -mcpu=970 -maltivec -o rustc_ppc rustc_100_percent.c
```

## Usage

```bash
# Compile a Rust source file to PowerPC assembly
./rustc_ppc hello.rs -o hello.s

# Assemble and link
as -o hello.o hello.s
gcc -o hello hello.o
```

## Supported Rust Features

- [x] Functions and return values
- [x] Variables and let bindings
- [x] Basic types (i32, u32, bool, etc.)
- [x] If/else expressions
- [x] Loops (loop, while, for)
- [x] Match expressions
- [x] Structs and enums
- [x] Traits and impl blocks
- [x] Generics (basic)
- [x] Closures
- [x] References and borrowing
- [x] Modules
- [ ] Lifetimes (partial)
- [ ] Async/await (not supported)
- [ ] Macros (limited)

## Why PowerPC?

- Run Rust on vintage Macs
- AltiVec provides 4x SIMD speedup
- Perfect for embedded/retro computing
- Keeps 20-year-old hardware relevant

## Project Status

This is a work-in-progress port. The compiler can compile substantial Rust programs to working PowerPC binaries.

### Current Limitations
- No std library (use core/alloc)
- Limited macro support
- No async runtime
- Manual memory management preferred

## Related Projects

- [ppc-tiger-tools](https://github.com/Scottcjn/ppc-tiger-tools) - Tools for Tiger/Leopard
- [llama-cpp-tigerleopard](https://github.com/Scottcjn/llama-cpp-tigerleopard) - LLM inference on G4/G5

## License

MIT License

---

*"Rust on your 2005 Power Mac. Because why not?"*
