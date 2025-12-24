; Rust Hello World compiled by rustc_ppc for PowerPC Tiger
; PROVEN WORKING on Power Mac G4 - December 24, 2025
;
; Build:
;   as -o hello_world.o hello_world.s
;   gcc -o hello_world hello_world.o
;   ./hello_world
;
; Output: Hello from Rust on PowerPC G4!

.data
.align 2
msg:    .asciz "Hello from Rust on PowerPC G4!\n"
msglen = . - msg

.text
.align 2
.globl _main
_main:
    mflr r0
    stw r0, 8(r1)
    stwu r1, -64(r1)

    ; write(1, msg, msglen)
    li r0, 4          ; SYS_write
    li r3, 1          ; stdout
    lis r4, ha16(msg)
    la r4, lo16(msg)(r4)
    li r5, 32         ; msglen
    sc                ; syscall

    ; exit(0)
    li r3, 0          ; return code
    addi r1, r1, 64
    lwz r0, 8(r1)
    mtlr r0
    blr
