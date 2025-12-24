/*
 * Rust Standard Library for Mac OS X Tiger/Leopard PowerPC
 *
 * Provides core/alloc/std bindings for Tiger (10.4) and Leopard (10.5)
 * Maps Rust abstractions to Darwin/BSD syscalls
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * TIGER/LEOPARD SYSTEM INTERFACE
 * ============================================================
 *
 * Tiger uses Mach-O and BSD syscalls.
 * We generate PowerPC assembly that calls these directly.
 */

/* Darwin syscall numbers (ppc) */
#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_mmap        197
#define SYS_munmap      73
#define SYS_mprotect    74

/* ============================================================
 * MEMORY ALLOCATION (alloc crate)
 * ============================================================ */

void emit_global_allocator() {
    printf("; Global Allocator for Tiger/Leopard\n");
    printf("; Uses system malloc/free via dyld\n\n");

    printf(".section __DATA,__data\n");
    printf(".align 2\n");
    printf("___rust_alloc_error_handler:\n");
    printf("    .long __ZN5alloc5alloc18handle_alloc_error17h0000000000000000E\n");
    printf("\n");

    /* __rust_alloc */
    printf(".text\n.align 2\n");
    printf(".globl ___rust_alloc\n");
    printf("___rust_alloc:\n");
    printf("    ; r3 = size, r4 = align\n");
    printf("    ; For now, ignore alignment and use malloc\n");
    printf("    b _malloc\n\n");

    /* __rust_alloc_zeroed */
    printf(".globl ___rust_alloc_zeroed\n");
    printf("___rust_alloc_zeroed:\n");
    printf("    ; r3 = size, r4 = align\n");
    printf("    mr r4, r3         ; count = size\n");
    printf("    li r3, 1          ; size = 1\n");
    printf("    b _calloc\n\n");

    /* __rust_dealloc */
    printf(".globl ___rust_dealloc\n");
    printf("___rust_dealloc:\n");
    printf("    ; r3 = ptr, r4 = size, r5 = align\n");
    printf("    b _free\n\n");

    /* __rust_realloc */
    printf(".globl ___rust_realloc\n");
    printf("___rust_realloc:\n");
    printf("    ; r3 = ptr, r4 = old_size, r5 = align, r6 = new_size\n");
    printf("    mr r4, r6         ; new size\n");
    printf("    b _realloc\n\n");
}

/* ============================================================
 * PANIC RUNTIME
 * ============================================================ */

void emit_panic_runtime() {
    printf("; Panic Runtime for Tiger/Leopard\n\n");

    /* rust_begin_panic */
    printf(".globl __ZN3std9panicking11begin_panic17h0000000000000000E\n");
    printf("__ZN3std9panicking11begin_panic17h0000000000000000E:\n");
    printf("    ; r3 = message ptr\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -64(r1)\n");
    printf("    \n");
    printf("    ; Print panic message\n");
    printf("    mr r4, r3         ; message\n");
    printf("    lis r3, ha16(Lpanic_prefix)\n");
    printf("    la r3, lo16(Lpanic_prefix)(r3)\n");
    printf("    bl _printf\n");
    printf("    \n");
    printf("    ; Call abort\n");
    printf("    bl _abort\n");
    printf("    \n");
    printf(".section __TEXT,__cstring\n");
    printf("Lpanic_prefix:\n");
    printf("    .asciz \"thread 'main' panicked at '%%s'\\n\"\n");
    printf(".text\n\n");

    /* panic_bounds_check */
    printf(".globl __ZN4core9panicking18panic_bounds_check17h0000000000000000E\n");
    printf("__ZN4core9panicking18panic_bounds_check17h0000000000000000E:\n");
    printf("    lis r3, ha16(Lbounds_msg)\n");
    printf("    la r3, lo16(Lbounds_msg)(r3)\n");
    printf("    b __ZN3std9panicking11begin_panic17h0000000000000000E\n");
    printf("\n");
    printf(".section __TEXT,__cstring\n");
    printf("Lbounds_msg:\n");
    printf("    .asciz \"index out of bounds\"\n");
    printf(".text\n\n");
}

/* ============================================================
 * I/O (std::io)
 * ============================================================ */

void emit_io_runtime() {
    printf("; I/O Runtime for Tiger/Leopard\n\n");

    /* stdout write */
    printf(".globl __ZN3std2io5stdio6_print17h0000000000000000E\n");
    printf("__ZN3std2io5stdio6_print17h0000000000000000E:\n");
    printf("    ; r3 = Arguments struct ptr\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -64(r1)\n");
    printf("    \n");
    printf("    ; Extract format string and args\n");
    printf("    lwz r4, 0(r3)     ; pieces ptr\n");
    printf("    lwz r5, 8(r3)     ; args ptr\n");
    printf("    \n");
    printf("    ; For simplicity, just print first piece\n");
    printf("    lwz r3, 0(r4)     ; first piece ptr\n");
    printf("    bl _printf\n");
    printf("    \n");
    printf("    addi r1, r1, 64\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n\n");

    /* stdin read_line */
    printf(".globl __ZN3std2io5stdio5stdin17h0000000000000000E\n");
    printf("__ZN3std2io5stdio5stdin17h0000000000000000E:\n");
    printf("    ; Return stdin handle (0)\n");
    printf("    li r3, 0\n");
    printf("    blr\n\n");

    /* File operations */
    printf(".globl __ZN3std2fs4File4open17h0000000000000000E\n");
    printf("__ZN3std2fs4File4open17h0000000000000000E:\n");
    printf("    ; r3 = path ptr\n");
    printf("    li r4, 0          ; O_RDONLY\n");
    printf("    li r5, 0          ; mode\n");
    printf("    li r0, %d         ; SYS_open\n", SYS_open);
    printf("    sc\n");
    printf("    blr\n\n");

    printf(".globl __ZN3std2fs4File5close17h0000000000000000E\n");
    printf("__ZN3std2fs4File5close17h0000000000000000E:\n");
    printf("    ; r3 = fd\n");
    printf("    li r0, %d         ; SYS_close\n", SYS_close);
    printf("    sc\n");
    printf("    blr\n\n");
}

/* ============================================================
 * THREAD/SYNC (std::thread, std::sync)
 * ============================================================ */

void emit_thread_runtime() {
    printf("; Thread Runtime for Tiger/Leopard\n");
    printf("; Uses pthreads via libSystem\n\n");

    /* thread::spawn stub */
    printf(".globl __ZN3std6thread5spawn17h0000000000000000E\n");
    printf("__ZN3std6thread5spawn17h0000000000000000E:\n");
    printf("    ; r3 = closure ptr\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -96(r1)\n");
    printf("    \n");
    printf("    ; Create thread via pthread_create\n");
    printf("    la r3, 64(r1)     ; thread_t ptr\n");
    printf("    li r4, 0          ; attr = NULL\n");
    printf("    lis r5, ha16(_rust_thread_start)\n");
    printf("    la r5, lo16(_rust_thread_start)(r5)\n");
    printf("    ; r6 = closure (already set)\n");
    printf("    bl _pthread_create\n");
    printf("    \n");
    printf("    lwz r3, 64(r1)    ; return thread handle\n");
    printf("    addi r1, r1, 96\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n\n");

    /* Mutex */
    printf(".globl __ZN3std4sync5mutex5Mutex3new17h0000000000000000E\n");
    printf("__ZN3std4sync5mutex5Mutex3new17h0000000000000000E:\n");
    printf("    ; Allocate pthread_mutex_t\n");
    printf("    li r3, 64         ; sizeof(pthread_mutex_t) on Tiger\n");
    printf("    bl _malloc\n");
    printf("    mr r4, r3\n");
    printf("    li r5, 0          ; attr = NULL\n");
    printf("    bl _pthread_mutex_init\n");
    printf("    blr\n\n");

    printf(".globl __ZN3std4sync5mutex5Mutex4lock17h0000000000000000E\n");
    printf("__ZN3std4sync5mutex5Mutex4lock17h0000000000000000E:\n");
    printf("    b _pthread_mutex_lock\n\n");

    printf(".globl __ZN3std4sync5mutex5Mutex6unlock17h0000000000000000E\n");
    printf("__ZN3std4sync5mutex5Mutex6unlock17h0000000000000000E:\n");
    printf("    b _pthread_mutex_unlock\n\n");
}

/* ============================================================
 * COLLECTIONS (std::collections)
 * ============================================================ */

void emit_collections_runtime() {
    printf("; Collections Runtime\n\n");

    /* Vec implementation */
    printf("; Vec<T> layout: { ptr: *mut T, len: usize, cap: usize }\n\n");

    printf(".globl __ZN5alloc3vec12Vec$LT$T$GT$3new17h0000000000000000E\n");
    printf("__ZN5alloc3vec12Vec$LT$T$GT$3new17h0000000000000000E:\n");
    printf("    ; Return empty Vec\n");
    printf("    li r3, 0          ; ptr = null\n");
    printf("    li r4, 0          ; len = 0\n");
    printf("    li r5, 0          ; cap = 0\n");
    printf("    blr\n\n");

    printf(".globl __ZN5alloc3vec12Vec$LT$T$GT$4push17h0000000000000000E\n");
    printf("__ZN5alloc3vec12Vec$LT$T$GT$4push17h0000000000000000E:\n");
    printf("    ; r3 = &mut self (Vec), r4 = value\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -64(r1)\n");
    printf("    stw r3, 32(r1)    ; save vec ptr\n");
    printf("    stw r4, 36(r1)    ; save value\n");
    printf("    \n");
    printf("    ; Check if need to grow\n");
    printf("    lwz r5, 4(r3)     ; len\n");
    printf("    lwz r6, 8(r3)     ; cap\n");
    printf("    cmpw r5, r6\n");
    printf("    blt Lpush_no_grow\n");
    printf("    \n");
    printf("    ; Grow: new_cap = cap * 2 or 4 if 0\n");
    printf("    cmpwi r6, 0\n");
    printf("    bne Lpush_double\n");
    printf("    li r6, 4\n");
    printf("    b Lpush_realloc\n");
    printf("Lpush_double:\n");
    printf("    slwi r6, r6, 1    ; cap * 2\n");
    printf("Lpush_realloc:\n");
    printf("    lwz r3, 0(r3)     ; old ptr\n");
    printf("    slwi r4, r6, 2    ; new size in bytes (assuming 4-byte elements)\n");
    printf("    bl _realloc\n");
    printf("    lwz r7, 32(r1)    ; restore vec ptr\n");
    printf("    stw r3, 0(r7)     ; store new ptr\n");
    printf("    stw r6, 8(r7)     ; store new cap\n");
    printf("    \n");
    printf("Lpush_no_grow:\n");
    printf("    lwz r3, 32(r1)    ; vec ptr\n");
    printf("    lwz r4, 36(r1)    ; value\n");
    printf("    lwz r5, 4(r3)     ; len\n");
    printf("    lwz r6, 0(r3)     ; data ptr\n");
    printf("    slwi r7, r5, 2    ; offset\n");
    printf("    stwx r4, r6, r7   ; store value\n");
    printf("    addi r5, r5, 1    ; len++\n");
    printf("    stw r5, 4(r3)\n");
    printf("    \n");
    printf("    addi r1, r1, 64\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n\n");

    /* String */
    printf("; String is just Vec<u8> with UTF-8 guarantee\n");
    printf(".globl __ZN5alloc6string6String3new17h0000000000000000E\n");
    printf("__ZN5alloc6string6String3new17h0000000000000000E:\n");
    printf("    b __ZN5alloc3vec12Vec$LT$T$GT$3new17h0000000000000000E\n\n");

    /* HashMap stub */
    printf(".globl __ZN3std11collections4hash3map7HashMap3new17h0000000000000000E\n");
    printf("__ZN3std11collections4hash3map7HashMap3new17h0000000000000000E:\n");
    printf("    ; Return empty HashMap (simplified)\n");
    printf("    li r3, 32\n");
    printf("    bl _calloc\n");
    printf("    blr\n\n");
}

/* ============================================================
 * PROCESS/ENV (std::process, std::env)
 * ============================================================ */

void emit_process_runtime() {
    printf("; Process Runtime for Tiger/Leopard\n\n");

    /* exit */
    printf(".globl __ZN3std7process4exit17h0000000000000000E\n");
    printf("__ZN3std7process4exit17h0000000000000000E:\n");
    printf("    ; r3 = exit code\n");
    printf("    li r0, %d         ; SYS_exit\n", SYS_exit);
    printf("    sc\n");
    printf("    ; Never returns\n\n");

    /* env::args */
    printf(".globl __ZN3std3env4args17h0000000000000000E\n");
    printf("__ZN3std3env4args17h0000000000000000E:\n");
    printf("    ; Return iterator over command line args\n");
    printf("    ; For now, return empty iterator\n");
    printf("    li r3, 0\n");
    printf("    li r4, 0\n");
    printf("    blr\n\n");

    /* env::var */
    printf(".globl __ZN3std3env3var17h0000000000000000E\n");
    printf("__ZN3std3env3var17h0000000000000000E:\n");
    printf("    ; r3 = key ptr\n");
    printf("    b _getenv\n\n");
}

/* ============================================================
 * MAIN ENTRY POINT
 * ============================================================ */

void emit_rust_main_wrapper() {
    printf("; Rust main wrapper for Tiger/Leopard\n\n");

    printf(".globl _main\n");
    printf("_main:\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -64(r1)\n");
    printf("    stw r3, 32(r1)    ; argc\n");
    printf("    stw r4, 36(r1)    ; argv\n");
    printf("    \n");
    printf("    ; Initialize Rust runtime\n");
    printf("    bl ___rust_runtime_init\n");
    printf("    \n");
    printf("    ; Call user's main\n");
    printf("    lwz r3, 32(r1)    ; argc\n");
    printf("    lwz r4, 36(r1)    ; argv\n");
    printf("    bl __ZN4main4main17h0000000000000000E\n");
    printf("    \n");
    printf("    ; Cleanup\n");
    printf("    bl ___rust_runtime_cleanup\n");
    printf("    \n");
    printf("    li r3, 0          ; exit code\n");
    printf("    addi r1, r1, 64\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n\n");

    printf(".globl ___rust_runtime_init\n");
    printf("___rust_runtime_init:\n");
    printf("    ; Initialize global state, TLS, etc.\n");
    printf("    blr\n\n");

    printf(".globl ___rust_runtime_cleanup\n");
    printf("___rust_runtime_cleanup:\n");
    printf("    ; Run destructors, cleanup\n");
    printf("    blr\n\n");
}

/* ============================================================
 * FORMATTING (std::fmt)
 * ============================================================ */

void emit_fmt_runtime() {
    printf("; Formatting Runtime\n\n");

    printf(".globl __ZN4core3fmt5write17h0000000000000000E\n");
    printf("__ZN4core3fmt5write17h0000000000000000E:\n");
    printf("    ; r3 = output, r4 = args\n");
    printf("    ; Simplified: just call printf with first piece\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -64(r1)\n");
    printf("    \n");
    printf("    lwz r5, 0(r4)     ; pieces\n");
    printf("    lwz r3, 0(r5)     ; first piece\n");
    printf("    bl _printf\n");
    printf("    \n");
    printf("    li r3, 0          ; Ok(())\n");
    printf("    addi r1, r1, 64\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n\n");

    /* Display trait */
    printf("; Display::fmt for primitive types\n");
    printf(".globl __ZN4core3fmt3num3imp52$LT$impl$u20$core..fmt..Display$u20$for$u20$i32$GT$3fmt17h0000000000000000E\n");
    printf("__ZN4core3fmt3num3imp52$LT$impl$u20$core..fmt..Display$u20$for$u20$i32$GT$3fmt17h0000000000000000E:\n");
    printf("    ; r3 = &i32, r4 = &mut Formatter\n");
    printf("    lwz r3, 0(r3)     ; load value\n");
    printf("    lis r4, ha16(Lfmt_int)\n");
    printf("    la r4, lo16(Lfmt_int)(r4)\n");
    printf("    mr r5, r3\n");
    printf("    mr r3, r4\n");
    printf("    b _printf\n");
    printf("\n");
    printf(".section __TEXT,__cstring\n");
    printf("Lfmt_int:\n");
    printf("    .asciz \"%%d\"\n");
    printf(".text\n\n");
}

/* ============================================================
 * GENERATE ALL
 * ============================================================ */

void emit_full_stdlib() {
    printf("; =====================================================\n");
    printf("; Rust Standard Library for Mac OS X Tiger/Leopard\n");
    printf("; PowerPC 32-bit (G4/G5)\n");
    printf("; =====================================================\n\n");

    emit_global_allocator();
    emit_panic_runtime();
    emit_io_runtime();
    emit_thread_runtime();
    emit_collections_runtime();
    emit_process_runtime();
    emit_fmt_runtime();
    emit_rust_main_wrapper();

    printf("; =====================================================\n");
    printf("; External symbols (from libSystem.B.dylib)\n");
    printf("; =====================================================\n");
    printf(".section __TEXT,__text\n");
    printf(".indirect_symbol _malloc\n");
    printf(".indirect_symbol _calloc\n");
    printf(".indirect_symbol _realloc\n");
    printf(".indirect_symbol _free\n");
    printf(".indirect_symbol _printf\n");
    printf(".indirect_symbol _abort\n");
    printf(".indirect_symbol _getenv\n");
    printf(".indirect_symbol _pthread_create\n");
    printf(".indirect_symbol _pthread_mutex_init\n");
    printf(".indirect_symbol _pthread_mutex_lock\n");
    printf(".indirect_symbol _pthread_mutex_unlock\n");
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--emit") == 0) {
        emit_full_stdlib();
    } else if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        printf("; === Rust stdlib for Tiger/Leopard Demo ===\n\n");
        printf("; This generates PowerPC assembly for:\n");
        printf(";   - Memory allocation (malloc/free wrappers)\n");
        printf(";   - Panic handling\n");
        printf(";   - I/O (stdin/stdout/files)\n");
        printf(";   - Threads (pthreads)\n");
        printf(";   - Collections (Vec, String, HashMap)\n");
        printf(";   - Process/Environment\n");
        printf(";   - Formatting\n\n");

        printf("; Example: Generating allocator...\n");
        emit_global_allocator();
    } else {
        printf("Rust Standard Library Generator for Tiger/Leopard\n");
        printf("Usage:\n");
        printf("  %s --emit    Generate full stdlib assembly\n", argv[0]);
        printf("  %s --demo    Show demo output\n", argv[0]);
        printf("\nOutput can be assembled with:\n");
        printf("  %s --emit > stdlib.s\n", argv[0]);
        printf("  as -o stdlib.o stdlib.s\n");
        printf("  ar rcs librust_tiger.a stdlib.o\n");
    }
    return 0;
}
