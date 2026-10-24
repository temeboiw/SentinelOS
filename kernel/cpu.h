#pragma once
// GDT + TSS (IST stacks), IDT for all 256 vectors, exception dump, interrupt dispatch.
#include <stdint.h>
#include "serial.h"

// layout matches what isr_common pushes (r15 is at the lowest address)
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;                         // pushed by the per-vector stub
    uint64_t rip, cs, rflags, rsp, ss;              // pushed by the CPU
} Frame;

#define DISPATCH_VECTOR 0x2F                    // software interrupt that enters the scheduler (the vector NT uses)
static Frame *KiSchedule(Frame *f);             // defined in ke.h

#pragma pack(push, 1)
typedef struct { uint16_t limit; uint64_t base; } DescPtr;
typedef struct { uint16_t lo; uint16_t sel; uint8_t ist; uint8_t type; uint16_t mid; uint32_t hi; uint32_t zero; } IdtEntry;
typedef struct { uint32_t r0; uint64_t rsp[3]; uint64_t r1; uint64_t ist[7]; uint64_t r2; uint16_t r3; uint16_t iomap; } Tss;   // 104 bytes
#pragma pack(pop)

// Selectors:
//   0x08 kernel code, 0x10 kernel data
//   0x18 user code (DPL3), 0x20 user data (DPL3)
//   0x28 TSS (16-byte descriptor at indices 5+6)
// User CS = 0x1B (0x18|3), User SS = 0x23 (0x20|3)
#define KERNEL_CS  0x08
#define KERNEL_DS  0x10
#define USER_CS    0x1B
#define USER_DS    0x23
#define USER_TEB_BASE 0x000000007FFF0000ULL
#define TSS_SEL    0x28

#define CPU_MAX 64
typedef struct {
    uint64_t gdt[7];
    Tss tss;
    uint8_t ist_stack[3][8192] __attribute__((aligned(16)));
    uint32_t index;
    volatile int need_resched;
    void *scheduler_current;
    void *scheduler_idle;
    int32_t scheduler_quantum;
    void *current_process;
    uint32_t apic_id;
    volatile uint32_t scheduler_active;
    void *scheduler_deferred_thread;
} CpuLocal;
static CpuLocal cpu_locals[CPU_MAX] __attribute__((aligned(64)));
static IdtEntry idt[256];
static const uint64_t gdt_template[7] = {
    0,
    0x00AF9A000000FFFFULL,
    0x00CF92000000FFFFULL,
    0x00AFFA000000FFFFULL,
    0x00CFF2000000FFFFULL,
    0, 0
};

/* GS_BASE points at the current CPU's private state in kernel mode. */
static CpuLocal *cpu_local_current(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000101));
    return (CpuLocal *)(uintptr_t)(((uint64_t)hi << 32) | lo);
}
static void cpu_set_gs_base(CpuLocal *cpu) {
    uint64_t v = (uint64_t)(uintptr_t)cpu;
    __asm__ volatile("wrmsr" :: "c"(0xC0000101), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}
/* With kernel GS active, this becomes the user GS base after SWAPGS. */
static void cpu_set_user_gs_base(uint64_t base) {
    __asm__ volatile("wrmsr" :: "c"(0xC0000102), "a"((uint32_t)base),
                     "d"((uint32_t)(base >> 32)));
}
#define ki_need_resched (cpu_local_current()->need_resched)

// ---- per-vector stubs: push (fake) error code + vector, then share isr_common ----
#define STUB_NOERR(n) ".globl isr" #n "\nisr" #n ":\n push 0\n push " #n "\n jmp isr_common\n"
#define STUB_ERR(n)   ".globl isr" #n "\nisr" #n ":\n push " #n "\n jmp isr_common\n"

__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    STUB_NOERR(0)  STUB_NOERR(1)  STUB_NOERR(2)  STUB_NOERR(3)
    STUB_NOERR(4)  STUB_NOERR(5)  STUB_NOERR(6)  STUB_NOERR(7)
    STUB_ERR(8)    STUB_NOERR(9)  STUB_ERR(10)   STUB_ERR(11)
    STUB_ERR(12)   STUB_ERR(13)   STUB_ERR(14)   STUB_NOERR(15)
    STUB_NOERR(16) STUB_ERR(17)   STUB_NOERR(18) STUB_NOERR(19)
    STUB_NOERR(20) STUB_ERR(21)   STUB_NOERR(22) STUB_NOERR(23)
    STUB_NOERR(24) STUB_NOERR(25) STUB_NOERR(26) STUB_NOERR(27)
    STUB_NOERR(28) STUB_ERR(29)   STUB_ERR(30)   STUB_NOERR(31)
    // vectors 32..255: 224 stubs, 16 bytes each, no error code
    ".p2align 4\n"
    ".globl isr_irq_base\n"
    "isr_irq_base:\n"
    ".set vecn, 32\n"
    ".rept 224\n"
    ".p2align 4\n"
    ".byte 0x6A, 0x00\n"               // push 0
    ".byte 0x68\n"                     // push imm32 (the vector)
    ".long vecn\n"
    "jmp isr_common\n"
    ".set vecn, vecn+1\n"
    ".endr\n"
    "isr_common:\n"
    " push rax\n push rbx\n push rcx\n push rdx\n push rsi\n push rdi\n push rbp\n"
    " push r8\n push r9\n push r10\n push r11\n push r12\n push r13\n push r14\n push r15\n"
    " cld\n"
    /* User-origin interrupts need kernel GS before entering C. */
    " test byte ptr [rsp + 144], 3\n"
    " jz 1f\n"
    " swapgs\n"
    "1:\n"
    " mov rcx, rsp\n"
    " sub rsp, 32\n"
    " call isr_handler\n"
    " mov rsp, rax\n"
    /* The scheduler may select a user frame even for a kernel-origin interrupt. */
    " test byte ptr [rsp + 144], 3\n"
    " jz 2f\n"
    " swapgs\n"
    "2:\n"
    " pop r15\n pop r14\n pop r13\n pop r12\n pop r11\n pop r10\n pop r9\n pop r8\n"
    " pop rbp\n pop rdi\n pop rsi\n pop rdx\n pop rcx\n pop rbx\n pop rax\n"
    " add rsp, 16\n"
    " iretq\n"
    ".att_syntax prefix\n"
);

#define DECL(n) extern void isr##n(void);
DECL(0)  DECL(1)  DECL(2)  DECL(3)  DECL(4)  DECL(5)  DECL(6)  DECL(7)
DECL(8)  DECL(9)  DECL(10) DECL(11) DECL(12) DECL(13) DECL(14) DECL(15)
DECL(16) DECL(17) DECL(18) DECL(19) DECL(20) DECL(21) DECL(22) DECL(23)
DECL(24) DECL(25) DECL(26) DECL(27) DECL(28) DECL(29) DECL(30) DECL(31)
extern char isr_irq_base[];

static void (*const isr_table[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static const char *const exc_name[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow", "BOUND Range", "Invalid Opcode",
    "Device Not Available", "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved", "x87 FP", "Alignment Check",
    "Machine Check", "SIMD FP", "Virtualization", "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved",
};

// ---- interrupt dispatch (vectors >= 32) ----
typedef void (*KINTERRUPT_ROUTINE)(Frame *);
static KINTERRUPT_ROUTINE irq_handler[256];

static void KeConnectInterrupt(ULONG Vector, KINTERRUPT_ROUTINE Routine) { irq_handler[Vector & 255] = Routine; }
static void irq_stub_eoi(Frame *f) { (void)f; }

// called from isr_common; returns the frame to resume.  Breakpoint returns, other exceptions dump state and halt,
// vectors >= 32 are dispatched (then the scheduler may hand back a different thread's frame).
// Forward decls for usermode (defined in user.h)
static Frame *syscall_handler(Frame *f);
static Frame *user_exception_handler(Frame *f);
static void user_set_kernel_stack(uint64_t rsp0);

__attribute__((used)) Frame *isr_handler(Frame *f) {
    // Syscall via int 0x80 – must run before the generic vector>=32 path
    if (f->vector == 0x80) {
        return syscall_handler(f);
    }
    if (f->vector >= 32) {
        if (f->vector == 0xFF) return f;                        // APIC spurious interrupt: no EOI
        if (irq_handler[f->vector]) irq_handler[f->vector](f);
        else DbgPrint("unhandled interrupt %llu\n", f->vector);
        if (f->vector != DISPATCH_VECTOR) HalEndSystemInterrupt(0, (ULONG)f->vector);   // EOI (not for software interrupts)
        if (ki_need_resched) { ki_need_resched = 0; return KiSchedule(f); }
        return f;
    }
    if (f->vector == 3) {
        DbgPrint("[#BP] breakpoint, rip=%016llx\n", f->rip);
        return f;
    }
    /* A fault from ring 3 is an application failure, not a kernel failure.
     * Give the process manager a chance to terminate the offending process
     * and resume its parent before the kernel panic path is considered. */
    if ((f->cs & 3u) == 3u)
        return user_exception_handler(f);

    uint64_t cr2, cr3;
    __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));

    DbgPrint("\n*** EXCEPTION %llu: %s ***\n", f->vector, exc_name[f->vector]);
    DbgPrint("error=%016llx  rip=%016llx  cs=%04llx  rflags=%016llx\n", f->error, f->rip, f->cs, f->rflags);
    DbgPrint("rsp=%016llx  ss=%04llx  cr2=%016llx  cr3=%016llx\n", f->rsp, f->ss, cr2, cr3);
    DbgPrint("rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", f->rax, f->rbx, f->rcx, f->rdx);
    DbgPrint("rsi=%016llx rdi=%016llx rbp=%016llx r8 =%016llx\n", f->rsi, f->rdi, f->rbp, f->r8);
    DbgPrint("r9 =%016llx r10=%016llx r11=%016llx r12=%016llx\n", f->r9, f->r10, f->r11, f->r12);
    DbgPrint("r13=%016llx r14=%016llx r15=%016llx\n", f->r13, f->r14, f->r15);
    DbgPrint("system halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
}

static void set_gate(int v, uint64_t a, int ist) {
    idt[v].lo   = (uint16_t)a;
    idt[v].sel  = 0x08;
    idt[v].ist  = (uint8_t)ist;
    idt[v].type = 0x8E;                             // present, ring 0, interrupt gate
    idt[v].mid  = (uint16_t)(a >> 16);
    idt[v].hi   = (uint32_t)(a >> 32);
    idt[v].zero = 0;
}

// DPL=3 interrupt gate (user-callable, e.g. syscall via int 0x80)
static void set_user_gate(int v, uint64_t a) {
    idt[v].lo   = (uint16_t)a;
    idt[v].sel  = 0x08;
    idt[v].ist  = 0;
    idt[v].type = 0xEE;                             // present, ring 3, interrupt gate
    idt[v].mid  = (uint16_t)(a >> 16);
    idt[v].hi   = (uint32_t)(a >> 32);
    idt[v].zero = 0;
}

static void set_tss_descriptor(CpuLocal *cpu) {
    uint64_t base = (uint64_t)&cpu->tss, limit = sizeof(Tss) - 1;
    cpu->gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
                  (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    cpu->gdt[6] = base >> 32;
}

// Keep TSS.RSP0 pointing at a valid kernel stack so interrupts from ring 3 land safely.
static void user_set_kernel_stack(uint64_t rsp0) {
    CpuLocal *cpu = cpu_local_current();
    if (cpu) cpu->tss.rsp[0] = rsp0;
}

static void cpu_init_cpu(uint32_t cpu_index) {
    // User images may contain compiler-generated SSE2/XMM instructions. UEFI
    // firmware does not guarantee that the OS-facing FPU/SSE control bits are
    // left enabled, so establish the architectural state before entering ring 3.
    // CR0.EM=0 enables x87/SSE; CR0.TS=0 avoids lazy-FPU #NM faults; CR0.MP=1
    // enables the expected WAIT/FWAIT behavior. CR4.OSFXSR and OSXMMEXCPT enable
    // SSE register state and SIMD floating-point exception delivery.
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1ULL << 2) | (1ULL << 3)); // clear EM and TS
    cr0 |= (1ULL << 1);                 // set MP
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);  // OSFXSR | OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    uint32_t mxcsr = 0x1F80;             // architectural default MXCSR
    __asm__ volatile("fninit; ldmxcsr %0" :: "m"(mxcsr) : "memory");

    if (cpu_index >= CPU_MAX) {
        DbgPrint("cpu: invalid CPU index %u\n", cpu_index);
        for (;;) __asm__ volatile("cli; hlt");
    }
    CpuLocal *cpu = &cpu_locals[cpu_index];
    cpu->index = cpu_index;
    for (int i = 0; i < 7; i++) cpu->gdt[i] = gdt_template[i];
    for (int i = 0; i < 3; i++)
        cpu->tss.ist[i] = (uint64_t)cpu->ist_stack[i] + sizeof cpu->ist_stack[i];
    cpu->tss.iomap = sizeof(Tss);
    set_tss_descriptor(cpu);

    DescPtr g = { sizeof cpu->gdt - 1, (uint64_t)cpu->gdt };
    __asm__ volatile("lgdt %0" :: "m"(g));
    __asm__ volatile(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        ::: "rax", "memory");

    if (cpu_index == 0) {
        for (int i = 0; i < 32; i++)
            set_gate(i, (uint64_t)isr_table[i], i == 8 ? 1 : i == 2 ? 2 : i == 18 ? 3 : 0);
        for (int i = 32; i < 256; i++)
            set_gate(i, (uint64_t)isr_irq_base + (uint64_t)(i - 32) * 16, 0);
        set_user_gate(0x80, (uint64_t)isr_irq_base + (uint64_t)(0x80 - 32) * 16);
    }
    DescPtr d = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" :: "m"(d));
    /* Loading GS selector resets the base, so install GS_BASE last. */
    cpu_set_gs_base(cpu);
}
static void cpu_init(void) { cpu_init_cpu(0); }
