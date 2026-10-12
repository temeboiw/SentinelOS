#pragma once
// Prints the module list handed over by loadufi and exercises the hal.dll exports.
#include "bootinfo.h"
#include "serial.h"

#ifndef TEST_HAL
#define TEST_HAL 0      // 1 = reboot after the HAL test, 2 = power off (QEMU)
#endif

static void modules_dump(const BootInfo *bi) {
    DbgPrint("\n-- loaded modules (from loadufi) --\n");
    for (ULONG i = 0; i < bi->module_count; i++)
        DbgPrint("  %s  base=%p  size=%llu KiB  entry=%p\n", bi->modules[i].name,
                 (void *)bi->modules[i].base, bi->modules[i].size / 1024, (void *)bi->modules[i].entry);
}

static void hal_selftest(void) {
    DbgPrint("\n-- HAL (hal.dll) --\n");
    LARGE_INTEGER freq;
    LARGE_INTEGER t0 = KeQueryPerformanceCounter(&freq);
    KeStallExecutionProcessor(50000);
    LARGE_INTEGER t1 = KeQueryPerformanceCounter(NULL);
    ULONGLONG us = (ULONGLONG)(t1.QuadPart - t0.QuadPart) * 1000000ULL / (ULONGLONG)freq.QuadPart;
    DbgPrint("hal test: counter %llu Hz, KeStallExecutionProcessor(50000) took %llu us: %s\n",
             (ULONGLONG)freq.QuadPart, us, (us >= 50000 && us < 100000) ? "ok" : "CHECK");

    UCHAR lsr = READ_PORT_UCHAR((PUCHAR)0x3FD);
    DbgPrint("hal test: READ_PORT_UCHAR(0x3FD) = 0x%x, COM1 transmitter idle: %s\n", (unsigned)lsr, (lsr & 0x20) ? "ok" : "CHECK");

#if TEST_HAL == 1
    DbgPrint("hal test: rebooting...\n");
    HalReturnToFirmware(HalRebootRoutine);
#elif TEST_HAL == 2
    DbgPrint("hal test: powering off...\n");
    HalReturnToFirmware(HalPowerDownRoutine);
#endif
}

// ---------------------------------------------------------------- scheduler test
static volatile int t_stop;
static volatile ULONGLONG t_count[8];
static volatile ULONG t_errors;

static void CounterThread(PVOID a) { ULONG i = (ULONG)(ULONGLONG)a; while (!t_stop) t_count[i]++; }

static void AllocThread(PVOID a) {                                  // heap + pattern checks under preemption
    ULONG i = (ULONG)(ULONGLONG)a; ULONGLONG seed = 12345 + i;
    while (!t_stop) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t n = 16 + (size_t)((seed >> 33) % 3000);
        uint8_t *p = kmalloc(n);
        if (!p) { t_errors++; continue; }
        for (size_t j = 0; j < n; j++) p[j] = (uint8_t)(i * 17 + j);
        if (i & 1) KeYield();                                       // give another thread a chance to scribble
        for (size_t j = 0; j < n; j++) if (p[j] != (uint8_t)(i * 17 + j)) { t_errors++; break; }
        kfree(p);
        t_count[i]++;
    }
}

static void SleeperThread(PVOID a) { ULONG i = (ULONG)(ULONGLONG)a; while (!t_stop) { KeSleep(50); t_count[i]++; } }

static void scheduler_selftest(const BootInfo *bi) {
    DbgPrint("\n-- scheduler (kernel threads, round-robin on the 100 Hz clock) --\n");
    if (!HalInitSystem(1, (PVOID)bi)) { DbgPrint("sched test: APIC init failed\n"); return; }
    KiInitializeScheduler();
    uint64_t err_before = h_errors, in_use_before = h_in_use;      // baseline includes Main and Idle
    for (ULONG i = 0; i < 3; i++) KeCreateThread("counter", CounterThread, (PVOID)(ULONGLONG)i);
    for (ULONG i = 3; i < 5; i++) KeCreateThread("alloc",   AllocThread,   (PVOID)(ULONGLONG)i);
    KeCreateThread("sleeper", SleeperThread, (PVOID)5ULL);

    if (!HalStartClockInterrupt(100, CLOCK_VECTOR)) { DbgPrint("sched test: cannot start the clock\n"); return; }
    LARGE_INTEGER f;
    LARGE_INTEGER t0 = KeQueryPerformanceCounter(&f);
    __asm__ volatile("sti");
    KeSleep(1000);                                                  // the workers run while Main sleeps
    LARGE_INTEGER t1 = KeQueryPerformanceCounter(NULL);
    ULONGLONG ms = (ULONGLONG)(t1.QuadPart - t0.QuadPart) * 1000ULL / (ULONGLONG)f.QuadPart;
    DbgPrint("sched test: Main slept 1000 ms and woke after %llu ms: %s\n", ms, (ms >= 990 && ms <= 1050) ? "ok" : "CHECK");

    t_stop = 1;
    for (int n = 0; n < 200; n++) {                                 // wait for every worker to return from its entry function
        int alive = 0;
        for (int i = 2; i < MAX_THREADS; i++) if (ki_threads[i] && ki_threads[i]->state != ThreadTerminated) alive++;
        if (!alive) break;
        KeSleep(10);
    }
    ULONG reaped = KeReapThreads();

    ULONGLONG cmin = t_count[0], cmax = t_count[0];
    for (int i = 1; i < 3; i++) { if (t_count[i] < cmin) cmin = t_count[i]; if (t_count[i] > cmax) cmax = t_count[i]; }
    DbgPrint("sched test: 3 CPU-bound threads never yield, yet all ran (timer preemption): %s\n", cmin ? "ok" : "FAIL");
    DbgPrint("sched test: fairness, busiest/laziest = %llu/%llu loops: %s\n", cmax, cmin, (cmin && cmax <= 2 * cmin) ? "ok" : "CHECK");
    DbgPrint("sched test: 2 alloc threads did %llu + %llu alloc/verify/free rounds, %u errors: %s\n",
             t_count[3], t_count[4], t_errors, (t_errors == 0 && t_count[3] && t_count[4]) ? "ok" : "FAIL");
    DbgPrint("sched test: sleeper woke %llu times in ~1 s (expect ~19-20): %s\n", t_count[5], (t_count[5] >= 15 && t_count[5] <= 21) ? "ok" : "CHECK");
    DbgPrint("sched test: %llu context switches, %llu clock ticks, %u threads reaped: %s\n",
             ki_switches, KeTickCount, reaped, reaped == 6 ? "ok" : "FAIL");
    DbgPrint("sched test: heap back to baseline after reaping (in-use %llu, new errors %llu): %s\n",
             h_in_use, h_errors - err_before, (h_in_use == in_use_before && h_errors == err_before) ? "ok" : "FAIL");
    uint64_t rfl; __asm__ volatile("pushfq; popq %0" : "=r"(rfl));
    DbgPrint("sched test: interrupts enabled (RFLAGS.IF): %s\n", (rfl & 0x200) ? "yes" : "NO");
}
