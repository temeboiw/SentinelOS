#pragma once
/*
 * SentinelOS SMP bring-up:
 * Discover processors through ACPI MADT, start APs with a low-memory INIT/SIPI
 * trampoline, and install each AP's private GDT/TSS/GS/APIC state. APs join the
 * shared locked scheduler with per-CPU current-thread/process state and a local
 * preemption timer. Process teardown refuses to free an address space active on
 * any CPU.
 */
#include <stdint.h>
#include "bootinfo.h"

extern uint8_t smp_trampoline_start[], smp_trampoline_end[];
extern uint8_t smp_tramp_gdtr[], smp_tramp_gdt[], smp_tramp_pm_far[], smp_tramp_pm_entry[];
extern uint8_t smp_tramp_cr3_imm[], smp_tramp_lm_far[], smp_tramp_lm_entry[];
extern uint8_t smp_tramp_stack_imm[], smp_tramp_index_imm[], smp_tramp_entry_imm[];

#define SMP_MAX_CPUS 64
#define SMP_MADT_LAPIC 0
#define SMP_MADT_X2APIC 9

typedef struct {
    uint32_t apic_id;
    uint32_t acpi_uid;
    uint8_t x2apic;
    uint8_t enabled;
} SmpCpuInfo;

typedef struct {
    uint32_t count;
    uint32_t bsp_apic_id;
    SmpCpuInfo cpus[SMP_MAX_CPUS];
} SmpTopology;

static SmpTopology smp_topology;

/* AP state is separate from topology: discovered does not mean started. */
#define SMP_CPU_OFFLINE  0u
#define SMP_CPU_STARTING 1u
#define SMP_CPU_ONLINE   2u
static volatile uint32_t smp_cpu_state[SMP_MAX_CPUS];
static volatile uint32_t smp_online_count;
static uint8_t smp_ap_stacks[SMP_MAX_CPUS][16384] __attribute__((aligned(16)));


static uint8_t smp_in8(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t smp_current_apic_id(void) {
    uint32_t a, b, c, d;
    a = 1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (d & (1u << 9)) {
        uint64_t apic_base;
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
        apic_base = ((uint64_t)hi << 32) | lo;
        if (apic_base & (1ULL << 10)) {
            uint32_t x2a, xb, xc, xd;
            x2a = 0x0B;
            __asm__ volatile("cpuid" : "+a"(x2a), "=b"(xb), "=c"(xc), "=d"(xd));
            if (xb && (xc & 0xFF) == 0x01) return xd;
        }
        if (apic_base & (1ULL << 11)) {
            volatile uint32_t *lapic = (volatile uint32_t *)(uintptr_t)(apic_base & 0xFFFFF000ULL);
            return lapic[0x20 / 4] >> 24;
        }
    }
    a = 1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return b >> 24;
}

static int smp_sig4(const char *p, const char *s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

static uint8_t *smp_find_rsdp(void) {
    /* EBDA base is in the BIOS Data Area; the first KiB is searched. */
    uint16_t ebda_seg = *(volatile uint16_t *)(uintptr_t)0x40E;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    for (uint32_t p = ebda; ebda && p < ebda + 1024; p += 16)
        if (smp_sig4((const char *)(uintptr_t)p, "RSD ")) return (uint8_t *)(uintptr_t)p;
    for (uint32_t p = 0xE0000; p < 0x100000; p += 16)
        if (smp_sig4((const char *)(uintptr_t)p, "RSD ")) return (uint8_t *)(uintptr_t)p;
    return 0;
}

static uint8_t smp_checksum(const uint8_t *p, uint32_t n) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum = (uint8_t)(sum + p[i]);
    return sum;
}

static void smp_add_cpu(uint32_t id, uint32_t uid, int x2, int enabled) {
    for (uint32_t i = 0; i < smp_topology.count; i++) {
        if (smp_topology.cpus[i].apic_id == id) {
            smp_topology.cpus[i].enabled |= (uint8_t)enabled;
            return;
        }
    }
    if (smp_topology.count >= SMP_MAX_CPUS) return;
    SmpCpuInfo *c = &smp_topology.cpus[smp_topology.count++];
    c->apic_id = id;
    c->acpi_uid = uid;
    c->x2apic = (uint8_t)x2;
    c->enabled = (uint8_t)enabled;
}

/* Called only after VMM has established identity mappings for low physical RAM. */
static void smp_discover(void) {
    smp_topology.count = 0;
    smp_topology.bsp_apic_id = smp_current_apic_id();
    cpu_locals[0].apic_id = smp_topology.bsp_apic_id;

    uint8_t *rsdp = smp_find_rsdp();
    if (!rsdp) {
        DbgPrint("smp: ACPI RSDP not found; AP startup disabled\n");
        return;
    }
    if (smp_checksum(rsdp, 20) != 0) {
        DbgPrint("smp: invalid RSDP checksum; AP startup disabled\n");
        return;
    }

    uint32_t rsdt_addr = *(uint32_t *)(rsdp + 16);
    uint64_t xsdt_addr = 0;
    uint32_t rsdp_len = 20;
    if (rsdp[15] >= 2) {
        rsdp_len = *(uint32_t *)(rsdp + 20);
        if (rsdp_len >= 36 && rsdp_len <= 4096 && smp_checksum(rsdp, rsdp_len) == 0)
            xsdt_addr = *(uint64_t *)(rsdp + 24);
    }

    uint8_t *root = 0;
    int wide = 0;
    if (xsdt_addr && xsdt_addr < 0x100000000ULL) {
        root = (uint8_t *)(uintptr_t)xsdt_addr;
        wide = 1;
    } else if (rsdt_addr) {
        root = (uint8_t *)(uintptr_t)rsdt_addr;
    }
    if (!root || !smp_sig4((const char *)root, wide ? "XSDT" : "RSDT")) {
        DbgPrint("smp: ACPI root table unavailable; AP startup disabled\n");
        return;
    }

    uint32_t root_len = *(uint32_t *)(root + 4);
    if (root_len < 36 || root_len > 1024 * 1024 || smp_checksum(root, root_len) != 0) {
        DbgPrint("smp: invalid ACPI root table; AP startup disabled\n");
        return;
    }

    uint32_t ent_size = wide ? 8 : 4;
    uint32_t ents = (root_len - 36) / ent_size;
    uint8_t *madt = 0;
    for (uint32_t i = 0; i < ents; i++) {
        uint64_t addr = wide ? *(uint64_t *)(root + 36 + i * 8)
                             : *(uint32_t *)(root + 36 + i * 4);
        if (addr && addr < 0x100000000ULL) {
            uint8_t *t = (uint8_t *)(uintptr_t)addr;
            if (smp_sig4((const char *)t, "APIC")) { madt = t; break; }
        }
    }
    if (!madt) {
        DbgPrint("smp: ACPI MADT not found; AP startup disabled\n");
        return;
    }

    uint32_t madt_len = *(uint32_t *)(madt + 4);
    if (madt_len < 44 || madt_len > 1024 * 1024 || smp_checksum(madt, madt_len) != 0) {
        DbgPrint("smp: invalid MADT; AP startup disabled\n");
        return;
    }

    uint32_t off = 44;
    while (off + 2 <= madt_len) {
        uint8_t type = madt[off], len = madt[off + 1];
        if (len < 2 || off + len > madt_len) break;
        if (type == SMP_MADT_LAPIC && len >= 8) {
            uint32_t uid = madt[off + 2], id = madt[off + 3];
            uint32_t flags = *(uint32_t *)(madt + off + 4);
            if (flags & 3) smp_add_cpu(id, uid, 0, 1);
        } else if (type == SMP_MADT_X2APIC && len >= 16) {
            uint32_t id = *(uint32_t *)(madt + off + 4);
            uint32_t flags = *(uint32_t *)(madt + off + 8);
            uint32_t uid = *(uint32_t *)(madt + off + 12);
            if (flags & 3) smp_add_cpu(id, uid, 1, 1);
        }
        off += len;
    }

    /* Reserve logical CPU 0 for the BSP regardless of MADT entry ordering. */
    for (uint32_t i = 0; i < smp_topology.count; i++) {
        if (smp_topology.cpus[i].apic_id == smp_topology.bsp_apic_id) {
            if (i != 0) {
                SmpCpuInfo tmp = smp_topology.cpus[0];
                smp_topology.cpus[0] = smp_topology.cpus[i];
                smp_topology.cpus[i] = tmp;
            }
            break;
        }
    }
    smp_cpu_state[0] = SMP_CPU_ONLINE;
    smp_online_count = 1;
    DbgPrint("smp: BSP APIC ID=%u; ACPI reports %u enabled processor(s)\n",
             smp_topology.bsp_apic_id, smp_topology.count);
    for (uint32_t i = 0; i < smp_topology.count; i++)
        DbgPrint("smp: cpu%u APIC ID=%u mode=%s%s\n", i,
                 smp_topology.cpus[i].apic_id,
                 smp_topology.cpus[i].x2apic ? "x2APIC" : "xAPIC",
                 smp_topology.cpus[i].apic_id == smp_topology.bsp_apic_id ? " (BSP)" : "");
    if (smp_topology.count == 0) {
        DbgPrint("smp: no enabled processors found in MADT\n");
        return;
    }
    /* AP startup follows discovery once the low-memory trampoline is installed. */
}

/*
 * Long-mode entry target for the low-memory INIT/SIPI trampoline.
 * Each AP installs private descriptor, process, scheduler, and APIC state
 * before publishing ONLINE. Its local periodic timer enters the scheduler
 * through a separate vector so only the BSP advances the global clock.
 */
static uint64_t smp_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static void smp_wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}
static void smp_delay(volatile uint32_t spins) {
    while (spins--) __asm__ volatile("pause");
}
static int smp_x2apic_enabled(void) {
    return (smp_rdmsr(0x1B) & (1ULL << 10)) != 0;
}
static void smp_lapic_write(uint32_t reg, uint32_t value) {
    if (smp_x2apic_enabled()) smp_wrmsr(0x800 + (reg >> 4), value);
    else {
        uint64_t base = smp_rdmsr(0x1B) & 0xFFFFF000ULL;
        ((volatile uint32_t *)(uintptr_t)base)[reg / 4] = value;
    }
}
static int smp_send_ipi(uint32_t destination, uint32_t command) {
    if (smp_x2apic_enabled()) {
        smp_wrmsr(0x830, ((uint64_t)destination << 32) | command);
        return 1;
    }
    if (destination > 255) return 0;
    uint64_t base = smp_rdmsr(0x1B) & 0xFFFFF000ULL;
    volatile uint32_t *lapic = (volatile uint32_t *)(uintptr_t)base;
    for (uint32_t i = 0; i < 1000000 && (lapic[0x300 / 4] & (1u << 12)); i++)
        __asm__ volatile("pause");
    lapic[0x310 / 4] = destination << 24;
    lapic[0x300 / 4] = command;
    for (uint32_t i = 0; i < 1000000 && (lapic[0x300 / 4] & (1u << 12)); i++)
        __asm__ volatile("pause");
    return !(lapic[0x300 / 4] & (1u << 12));
}
static void smp_enable_local_apic(void) {
    uint64_t base = smp_rdmsr(0x1B);
    if (!(base & (1ULL << 11))) smp_wrmsr(0x1B, base | (1ULL << 11));
    smp_lapic_write(0x80, 0);
    smp_lapic_write(0xF0, 0x1FF);
}

__attribute__((used, noreturn))
static void smp_ap_entry(uint32_t cpu_index) {
    __asm__ volatile("cli");
    if (cpu_index == 0 || cpu_index >= SMP_MAX_CPUS ||
        cpu_index >= smp_topology.count) {
        DbgPrint("smp: AP entered with invalid CPU index %u\n", cpu_index);
        for (;;) __asm__ volatile("cli; hlt");
    }

    smp_cpu_state[cpu_index] = SMP_CPU_STARTING;
    cpu_init_cpu(cpu_index);
    smp_enable_local_apic();
    cpu_locals[cpu_index].apic_id = smp_topology.cpus[cpu_index].apic_id;
    MmCurrentProcess = PspSystemProcess;
    __atomic_add_fetch(&PspSystemProcess->ActiveCpus, 1, __ATOMIC_ACQ_REL);
    if (!KiInitializeApScheduler(cpu_index)) {
        DbgPrint("smp: AP cpu%u scheduler initialization failed; parking\n", cpu_index);
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (!HalStartClockInterrupt(100, 0xF0)) {
        DbgPrint("smp: AP cpu%u local preemption timer failed; parking\n", cpu_index);
        for (;;) __asm__ volatile("cli; hlt");
    }
    cpu_locals[cpu_index].scheduler_active = 1;

    DbgPrint("smp: AP cpu%u (APIC ID=%u) online; shared scheduler + local preemption timer active\n",
             cpu_index, smp_topology.cpus[cpu_index].apic_id);
    __sync_fetch_and_add(&smp_online_count, 1);
    __asm__ volatile("" ::: "memory");
    smp_cpu_state[cpu_index] = SMP_CPU_ONLINE;

    for (;;) __asm__ volatile("sti; hlt");
}
static void smp_start_aps(const BootInfo *bi) {
    if (!bi || !bi->ap_trampoline_phys) {
        DbgPrint("smp: UEFI did not reserve a low-memory AP trampoline; BSP only\n");
        return;
    }
    if (smp_topology.count < 2) {
        DbgPrint("smp: no secondary processors to start\n");
        return;
    }
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;
    if (cr3 > 0xFFFFFFFFULL) {
        DbgPrint("smp: PML4 at %p exceeds 32-bit AP trampoline limit\n", (void *)cr3);
        return;
    }
    uint64_t apic_base = smp_rdmsr(0x1B);
    if (!(apic_base & (1ULL << 11))) smp_wrmsr(0x1B, apic_base | (1ULL << 11));
    smp_enable_local_apic();

    uint8_t *page = (uint8_t *)(uintptr_t)bi->ap_trampoline_phys;
    uintptr_t bytes = (uintptr_t)smp_trampoline_end - (uintptr_t)smp_trampoline_start;
    if (bytes > 4096 || (bi->ap_trampoline_phys & 0xFFFULL) ||
        bi->ap_trampoline_phys >= 0x100000ULL) {
        DbgPrint("smp: invalid SIPI page %p (template %llu bytes)\n",
                 (void *)bi->ap_trampoline_phys, (unsigned long long)bytes);
        return;
    }
    for (uintptr_t i = 0; i < 4096; i++) page[i] = 0;
    for (uintptr_t i = 0; i < bytes; i++) page[i] = smp_trampoline_start[i];

    uintptr_t gdtr_off = (uintptr_t)smp_tramp_gdtr - (uintptr_t)smp_trampoline_start;
    uintptr_t gdt_off = (uintptr_t)smp_tramp_gdt - (uintptr_t)smp_trampoline_start;
    uintptr_t pm_far_off = (uintptr_t)smp_tramp_pm_far - (uintptr_t)smp_trampoline_start;
    uintptr_t cr3_off = (uintptr_t)smp_tramp_cr3_imm - (uintptr_t)smp_trampoline_start;
    uintptr_t lm_far_off = (uintptr_t)smp_tramp_lm_far - (uintptr_t)smp_trampoline_start;
    uintptr_t stack_off = (uintptr_t)smp_tramp_stack_imm - (uintptr_t)smp_trampoline_start;
    uintptr_t index_off = (uintptr_t)smp_tramp_index_imm - (uintptr_t)smp_trampoline_start;
    uintptr_t entry_off = (uintptr_t)smp_tramp_entry_imm - (uintptr_t)smp_trampoline_start;
    uintptr_t pm_off = (uintptr_t)smp_tramp_pm_entry - (uintptr_t)smp_trampoline_start;
    uintptr_t lm_off = (uintptr_t)smp_tramp_lm_entry - (uintptr_t)smp_trampoline_start;
    *(uint32_t *)(page + gdtr_off + 2) = (uint32_t)(bi->ap_trampoline_phys + gdt_off);
    *(uint32_t *)(page + pm_far_off) = (uint32_t)(bi->ap_trampoline_phys + pm_off);
    *(uint32_t *)(page + cr3_off) = (uint32_t)cr3;
    *(uint32_t *)(page + lm_far_off) = (uint32_t)(bi->ap_trampoline_phys + lm_off);
    *(uint64_t *)(page + entry_off) = (uint64_t)(uintptr_t)smp_ap_entry;

    uint32_t vector = (uint32_t)(bi->ap_trampoline_phys >> 12);
    DbgPrint("smp: INIT/SIPI trampoline at %p, vector=%u, PML4=%p\n",
             (void *)bi->ap_trampoline_phys, vector, (void *)cr3);
    for (uint32_t i = 1; i < smp_topology.count; i++) {
        uint32_t id = smp_topology.cpus[i].apic_id;
        smp_cpu_state[i] = SMP_CPU_OFFLINE;
        *(uint64_t *)(page + stack_off) =
            (((uint64_t)(uintptr_t)smp_ap_stacks[i] + sizeof smp_ap_stacks[i]) & ~15ULL) - 8ULL;
        *(uint32_t *)(page + index_off) = i;
        if (!smp_send_ipi(id, 0x0000C500u)) {
            DbgPrint("smp: could not send INIT to APIC ID=%u; stopping AP startup\n", id);
            break;
        }
        smp_delay(20000000u);
        if (!smp_send_ipi(id, 0x00008500u)) {
            DbgPrint("smp: INIT deassert failed for APIC ID=%u; stopping AP startup\n", id);
            break;
        }
        smp_delay(2000000u);
        if (!smp_send_ipi(id, 0x00000600u | vector)) {
            DbgPrint("smp: first SIPI failed for APIC ID=%u; stopping AP startup\n", id);
            break;
        }
        smp_delay(2000000u);
        (void)smp_send_ipi(id, 0x00000600u | vector);
        uint32_t wait = 200000000u;
        while (wait-- && smp_cpu_state[i] != SMP_CPU_ONLINE)
            __asm__ volatile("pause");
        if (smp_cpu_state[i] != SMP_CPU_ONLINE) {
            DbgPrint("smp: APIC ID=%u failed to come online; stopping AP startup\n", id);
            break;
        }
        DbgPrint("smp: AP cpu%u confirmed online (%u/%u); shared scheduler active\n",
                 i, smp_online_count, smp_topology.count);
    }
    DbgPrint("smp: %u logical CPU(s) online; scheduler active on each initialized CPU\n",
             smp_online_count);
}
