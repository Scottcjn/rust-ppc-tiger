/*
 * malloc_wrapper.c — Provide _malloc symbol for non-PIC transpiled code
 *
 * The transpiler emits `bl _malloc` which creates a non-PIC relocation.
 * GCC's compiled version of this wrapper goes through the proper PIC stub.
 * We rename the transpiler's _malloc calls to _rust_malloc to avoid conflict.
 *
 * Actually: simpler approach — the transpiler emits bl _malloc, and this
 * file is compiled by GCC (which handles PIC correctly). We just need to
 * ensure the object files don't have the relocation issue.
 *
 * REAL FIX: Use -mdynamic-no-pic or -static link.
 */

/* This file intentionally left minimal — the real fix is the link flags */
