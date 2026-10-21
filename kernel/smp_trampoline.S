.intel_syntax noprefix
.section .text.smp_trampoline,"xr"
.code16
.globl smp_trampoline_start
.globl smp_trampoline_end
.globl smp_tramp_gdtr
.globl smp_tramp_gdt
.globl smp_tramp_pm_far
.globl smp_tramp_pm_entry
.globl smp_tramp_cr3_imm
.globl smp_tramp_lm_far
.globl smp_tramp_lm_entry
.globl smp_tramp_stack_imm
.globl smp_tramp_index_imm
.globl smp_tramp_entry_imm
smp_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    .byte 0x2e, 0x0f, 0x01, 0x16
    .word smp_tramp_gdtr - smp_trampoline_start
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .byte 0x66, 0xea
smp_tramp_pm_far:
    .long 0
    .word 0x08
.code32
smp_tramp_pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    .byte 0xb8
smp_tramp_cr3_imm:
    .long 0
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    .byte 0xea
smp_tramp_lm_far:
    .long 0
    .word 0x18
.code64
smp_tramp_lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    .byte 0x48, 0xbc
smp_tramp_stack_imm:
    .quad 0
    .byte 0xb9
smp_tramp_index_imm:
    .long 0
    .byte 0x48, 0xb8
smp_tramp_entry_imm:
    .quad 0
    jmp rax
.align 8
smp_tramp_gdtr:
    .word smp_tramp_gdt_end - smp_tramp_gdt - 1
    .long 0
    .word 0
.align 8
smp_tramp_gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
smp_tramp_gdt_end:
smp_trampoline_end:
