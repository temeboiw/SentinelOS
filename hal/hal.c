// hal.dll - Hardware Abstraction Layer.
// Owns the debug serial port (COM1), raw port I/O, the time source, the interrupt controller
// (local APIC + clock) and firmware re-entry.  Later: IOAPIC, bus access.
#ifndef HAL_BUILD
#define HAL_BUILD
#endif
#include "hal.h"

/*
 * Port I/O is a compiler ordering boundary for device-register sequences.
 * The memory clobber prevents ordinary loads/stores from being moved across
 * these operations by optimization.
 */
static inline UCHAR inb(USHORT p) {
    UCHAR v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p) : "memory");
    return v;
}
static inline USHORT inw(USHORT p) {
    USHORT v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p) : "memory");
    return v;
}
static inline ULONG inl(USHORT p) {
    ULONG v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p) : "memory");
    return v;
}
static inline VOID outb(USHORT p, UCHAR v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p) : "memory");
}
static inline VOID outw(USHORT p, USHORT v) {
    __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p) : "memory");
}
static inline VOID outl(USHORT p, ULONG v) {
    __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p) : "memory");
}

static inline VOID cpuid(ULONG leaf, ULONG subleaf,
                         ULONG *a, ULONG *b, ULONG *c, ULONG *d) {
    ULONG ra, rb, rc, rd;
    __asm__ volatile("cpuid"
                     : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
                     : "a"(leaf), "c"(subleaf));
    if (a) *a = ra;
    if (b) *b = rb;
    if (c) *c = rc;
    if (d) *d = rd;
}

static inline ULONGLONG rdtsc(void) {
    ULONG lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((ULONGLONG)hi << 32) | lo;
}

static BOOLEAN cpu_has_tsc(void) {
    ULONG max_leaf, a, b, c, d;
    cpuid(0, 0, &max_leaf, &b, &c, &d);
    if (max_leaf < 1) return FALSE;
    cpuid(1, 0, &a, &b, &c, &d);
    return (d & (1u << 4)) != 0;
}

static BOOLEAN cpu_has_apic(void) {
    ULONG max_leaf, a, b, c, d;
    cpuid(0, 0, &max_leaf, &b, &c, &d);
    if (max_leaf < 1) return FALSE;
    cpuid(1, 0, &a, &b, &c, &d);
    return (d & (1u << 9)) != 0;
}

static BOOLEAN cpu_has_x2apic(void) {
    ULONG max_leaf, a, b, c, d;
    cpuid(0, 0, &max_leaf, &b, &c, &d);
    if (max_leaf < 1) return FALSE;
    cpuid(1, 0, &a, &b, &c, &d);
    return (c & (1u << 21)) != 0;
}
static inline ULONGLONG rdmsr(ULONG msr) {
    ULONG lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((ULONGLONG)hi << 32) | lo;
}
static inline VOID wrmsr(ULONG msr, ULONGLONG v) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((ULONG)v), "d"((ULONG)(v >> 32)));
}

// ---------------------------------------------------------------- port I/O exports
HALAPI UCHAR  READ_PORT_UCHAR(PUCHAR Port)   { return inb((USHORT)(ULONGLONG)Port); }
HALAPI USHORT READ_PORT_USHORT(PUSHORT Port) { return inw((USHORT)(ULONGLONG)Port); }
HALAPI ULONG  READ_PORT_ULONG(PULONG Port)   { return inl((USHORT)(ULONGLONG)Port); }
HALAPI VOID   WRITE_PORT_UCHAR(PUCHAR Port, UCHAR Value)    { outb((USHORT)(ULONGLONG)Port, Value); }
HALAPI VOID   WRITE_PORT_USHORT(PUSHORT Port, USHORT Value) { outw((USHORT)(ULONGLONG)Port, Value); }
HALAPI VOID   WRITE_PORT_ULONG(PULONG Port, ULONG Value)    { outl((USHORT)(ULONGLONG)Port, Value); }

// ---------------------------------------------------------------- debug serial port
#define COM1 0x3F8

static void com1_init(void) {
    outb(COM1 + 1, 0x00);       // no interrupts
    outb(COM1 + 3, 0x80);       // DLAB on
    outb(COM1 + 0, 0x01);       // divisor 1 = 115200 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);       // 8N1
    outb(COM1 + 2, 0xC7);       // FIFO on, cleared
    outb(COM1 + 4, 0x0B);
}

static void com1_putc(char c) {
    for (ULONG i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20) {
            outb(COM1, (UCHAR)c);
            return;
        }
        __asm__ volatile("pause");
    }
    /* A stuck/missing UART must not stall boot forever or receive a write
       while its transmitter is still busy. Drop this character on timeout. */
}

HALAPI VOID HalDisplayString(PCSTR String) {
    if (!String) return;
    for (; *String; String++) {
        if (*String == '\n') com1_putc('\r');
        com1_putc(*String);
    }
}

static void hput_dec(ULONGLONG v) {
    char buf[24]; int n = 0;
    if (!v) buf[n++] = '0';
    while (v && n < 23) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) com1_putc(buf[n]);
}
static void hput_hex(ULONGLONG v) {
    HalDisplayString("0x");
    for (int s = 60; s >= 0; s -= 4) com1_putc("0123456789abcdef"[(v >> s) & 0xF]);
}

// ---------------------------------------------------------------- time source (TSC, calibrated against the PIT)
static ULONGLONG tsc_hz;
static ULONGLONG tsc_per_us;
static BOOLEAN   tsc_calibrated;

/*
 * PIT channel 2 is an independent fallback when TSC calibration is unavailable.
 * Keep each one-shot below 55 ms so its 16-bit count cannot wrap.
 */
static void pit_stall_chunk(ULONG microseconds) {
    if (!microseconds) return;
    ULONGLONG scaled = (ULONGLONG)microseconds * 1193182ULL;
    ULONG count = (ULONG)((scaled + 999999ULL) / 1000000ULL);
    if (!count) count = 1;
    if (count > 65535) count = 65535;

    UCHAR saved_gate = inb(0x61);
    UCHAR gate_off = (UCHAR)(saved_gate & 0xFC);
    outb(0x61, gate_off);
    outb(0x43, 0xB0);                 // PIT channel 2, lobyte/hibyte, mode 0
    outb(0x42, (UCHAR)count);
    outb(0x42, (UCHAR)(count >> 8));
    outb(0x61, (UCHAR)(gate_off | 1)); // gate on, speaker data disabled

    ULONG spins = 0;
    while (!(inb(0x61) & 0x20) && ++spins < 50000000UL)
        __asm__ volatile("pause" ::: "memory");

    outb(0x61, saved_gate);
}

static void calibrate_tsc(void) {
    const ULONG count = 11932;                  // 11932 / 1193182 Hz = 10 ms
    UCHAR gate = inb(0x61);
    outb(0x61, (UCHAR)((gate & 0xFC) | 0x01));  // PIT channel 2 gate on, speaker off
    outb(0x43, 0xB0);                           // channel 2, lobyte/hibyte, mode 0
    outb(0x42, (UCHAR)(count & 0xFF));
    outb(0x42, (UCHAR)(count >> 8));
    UCHAR v = (UCHAR)(inb(0x61) & 0xFE);        // restart the count: toggle the gate
    outb(0x61, v);
    outb(0x61, (UCHAR)(v | 1));
    ULONGLONG t0 = rdtsc();
    ULONG spins = 0;
    while (!(inb(0x61) & 0x20) && ++spins < 50000000UL)
        __asm__ volatile("pause" ::: "memory");
    ULONGLONG t1 = rdtsc();
    outb(0x61, gate);
    if (spins < 50000000UL && t1 > t0) {
        ULONGLONG delta = t1 - t0;
        /* Guard the scale operation even if a broken virtual clock jumps. */
        if (delta <= (~(ULONGLONG)0) / 100) {
            tsc_hz = delta * 100;
            /* Round up so short stalls are not systematically too short. */
            tsc_per_us = tsc_hz / 1000000ULL;
            if (tsc_hz % 1000000ULL) ++tsc_per_us;
            if (!tsc_per_us) tsc_per_us = 1;
            tsc_calibrated = TRUE;
        }
    }
}

HALAPI VOID KeStallExecutionProcessor(ULONG Microseconds) {
    if (!Microseconds) return;
    if (!tsc_calibrated || !tsc_per_us) {
        while (Microseconds) {
            ULONG chunk = Microseconds > 50000 ? 50000 : Microseconds;
            pit_stall_chunk(chunk);
            Microseconds -= chunk;
        }
        return;
    }

    ULONGLONG ticks = (ULONGLONG)Microseconds * tsc_per_us;
    ULONGLONG start = rdtsc();
    while ((ULONGLONG)(rdtsc() - start) < ticks)
        __asm__ volatile("pause" ::: "memory");
}

HALAPI LARGE_INTEGER KeQueryPerformanceCounter(PLARGE_INTEGER PerformanceFrequency) {
    LARGE_INTEGER r;
    /* A zero frequency explicitly reports that the HAL could not calibrate TSC. */
    if (PerformanceFrequency)
        PerformanceFrequency->QuadPart = tsc_calibrated ? (LONGLONG)tsc_hz : 0;
    r.QuadPart = (LONGLONG)rdtsc();
    return r;
}

// ---------------------------------------------------------------- interrupt controller: local APIC + clock
#define MSR_APIC_BASE 0x1B
#define LAPIC_TPR     0x80
#define LAPIC_EOI     0xB0
#define LAPIC_SVR     0xF0
#define LAPIC_LVT_TMR 0x320
#define LAPIC_TMR_INI 0x380
#define LAPIC_TMR_CUR 0x390
#define LAPIC_TMR_DIV 0x3E0

static volatile ULONG *lapic;                   // xAPIC register window (physical == virtual: identity-mapped)
static BOOLEAN   x2apic, apic_ready;
static ULONGLONG apic_timer_hz;                 // timer ticks per second at divide-by-16

static ULONG lapic_read(ULONG reg)  { return x2apic ? (ULONG)rdmsr(0x800 + (reg >> 4)) : lapic[reg / 4]; }
static VOID  lapic_write(ULONG reg, ULONG v) { if (x2apic) wrmsr(0x800 + (reg >> 4), v); else lapic[reg / 4] = v; }

// the legacy 8259s must not fire into the exception vectors: remap them out of the way and mask everything
static void pic_disable(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);         // ICW1: init
    outb(0x21, 0x20); outb(0xA1, 0x28);         // ICW2: vectors 0x20..0x2F
    outb(0x21, 0x04); outb(0xA1, 0x02);         // ICW3: cascade
    outb(0x21, 0x01); outb(0xA1, 0x01);         // ICW4: 8086 mode
    outb(0x21, 0xFF); outb(0xA1, 0xFF);         // mask all
}

static BOOLEAN apic_init(void) {
    /*
     * Timer calibration below uses the TSC as its reference. Do not publish
     * a guessed APIC frequency if the PIT-based TSC calibration failed.
     */
    if (!tsc_calibrated) {
        HalDisplayString("HAL: refusing APIC timer calibration with an uncalibrated TSC\n");
        return FALSE;
    }

    /* Never touch the APIC MSR/register window unless CPUID advertises it. */
    if (!cpu_has_apic()) {
        HalDisplayString("HAL: CPU does not advertise a local APIC\n");
        return FALSE;
    }

    ULONGLONG base = rdmsr(MSR_APIC_BASE);
    if ((base & (1ULL << 10)) && !cpu_has_x2apic()) {
        HalDisplayString("HAL: x2APIC enabled but unsupported by CPUID\n");
        return FALSE;
    }
    if (!(base & (1ULL << 11))) {
        base |= 1ULL << 11;
        wrmsr(MSR_APIC_BASE, base);
        base = rdmsr(MSR_APIC_BASE);
        if (!(base & (1ULL << 11))) {
            HalDisplayString("HAL: failed to enable local APIC\n");
            return FALSE;
        }
    }
    x2apic = (base & (1ULL << 10)) != 0;
    /*
     * IA32_APIC_BASE carries the physical address in bits 12 and above.
     * Preserve the full physical-address field; a 32-bit mask truncates
     * systems whose local APIC window is mapped above 4 GiB.
     */
    ULONGLONG apic_phys = base & 0xFFFFFFFFFFFFF000ULL;
    if (!x2apic && !apic_phys) {
        HalDisplayString("HAL: invalid local APIC physical base\n");
        return FALSE;
    }
    lapic = (volatile ULONG *)(uintptr_t)apic_phys;

    lapic_write(LAPIC_TPR, 0);                  // accept every priority
    lapic_write(LAPIC_SVR, 0x100 | 0xFF);       // APIC software-enable, spurious vector 0xFF

    // Calibrate before masking the legacy PIC; failure must not strand the machine
    // with both the local APIC timer unavailable and all 8259 interrupts masked.
    lapic_write(LAPIC_TMR_DIV, 0x3);
    lapic_write(LAPIC_LVT_TMR, 0x10000);        // masked
    lapic_write(LAPIC_TMR_INI, 0xFFFFFFFF);
    ULONGLONG calibration_start = rdtsc();
    ULONGLONG calibration_ticks = tsc_per_us * 10000;
    /* Unsigned elapsed-time arithmetic remains valid across TSC wraparound. */
    while ((ULONGLONG)(rdtsc() - calibration_start) < calibration_ticks)
        __asm__ volatile("pause" ::: "memory");
    ULONG left = lapic_read(LAPIC_TMR_CUR);
    lapic_write(LAPIC_TMR_INI, 0);              // stop
    apic_timer_hz = (ULONGLONG)(0xFFFFFFFFUL - left) * 100;
    if (!apic_timer_hz) {
        HalDisplayString("HAL: local APIC timer calibration failed; leaving legacy PIC enabled\n");
        return FALSE;
    }

    pic_disable();
    return TRUE;
}

HALAPI BOOLEAN HalStartClockInterrupt(ULONG Hz, ULONG Vector) {
    if (!apic_ready || !Hz || Vector < 0x20 || Vector >= 0xFF)
        return FALSE;

    ULONGLONG initial = apic_timer_hz / Hz;
    /* A zero or >32-bit initial count cannot produce the requested period. */
    if (!initial || initial > 0xFFFFFFFFULL)
        return FALSE;

    lapic_write(LAPIC_TMR_DIV, 0x3);
    lapic_write(LAPIC_LVT_TMR, (Vector & 0xFF) | 0x20000);       // periodic mode
    lapic_write(LAPIC_TMR_INI, (ULONG)initial);
    return TRUE;
}

HALAPI VOID HalEndSystemInterrupt(KIRQL NewIrql, ULONG Vector) {
    (void)NewIrql;
    if (Vector >= 0x20 && Vector <= 0x2F) {
        if (Vector >= 0x28) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
    if (apic_ready) lapic_write(LAPIC_EOI, 0);
}

// ---------------------------------------------------------------- init / firmware re-entry
HALAPI BOOLEAN HalInitSystem(ULONG Phase, PVOID LoaderBlock) {
    (void)LoaderBlock;
    if (Phase == 0) {
        com1_init();
        if (cpu_has_tsc())
            calibrate_tsc();
        else
            HalDisplayString("HAL: CPU does not advertise TSC\n");
        HalDisplayString("HAL: debug port COM1 ready, TSC ");
        if (tsc_calibrated) {
            hput_dec(tsc_hz / 1000000);
            HalDisplayString(" MHz (calibrated against the PIT)\n");
        } else {
            HalDisplayString("frequency unavailable; PIT stall fallback enabled\n");
        }
    } else if (Phase == 1) {
        apic_ready = apic_init();
        if (apic_ready) {
            HalDisplayString("HAL: local APIC at ");
            hput_hex((ULONGLONG)lapic);
            HalDisplayString(x2apic ? " (x2APIC)" : " (xAPIC)");
            HalDisplayString(", timer ");
            hput_dec(apic_timer_hz / 1000000);
            HalDisplayString(" MHz at divide-by-16; 8259 PIC masked\n");
        } else {
            HalDisplayString("HAL: local APIC initialization failed; legacy PIC was not necessarily masked\n");
        }
        return apic_ready;
    }
    return TRUE;
}

HALAPI VOID HalReturnToFirmware(FIRMWARE_REENTRY Routine) {
    if (Routine == HalPowerDownRoutine) {       // emulator ACPI power-off ports; real hardware needs ACPI (later)
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
    } else if (Routine == HalRestartRoutine || Routine == HalRebootRoutine) {
        outb(0xCF9, 0x06);                      // PCI reset control
        outb(0x64, 0xFE);                       // keyboard-controller reset line
    }
    for (;;) __asm__ volatile("cli; hlt");
}
