#pragma once
/*
 * SentinelOS SMP bring-up, phase 1:
 * Discover xAPIC processor IDs from the ACPI MADT. This deliberately does
 * not start APs yet; the current kernel still has BSP-global scheduler,
 * TSS and interrupt state, so running another core before those are made
 * per-CPU would corrupt live process state.
 */
#include <stdint.h>

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
    DbgPrint("smp: BSP APIC ID=%u; ACPI reports %u enabled processor(s)\\n",
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
    DbgPrint("smp: AP startup is not enabled until per-CPU TSS and scheduler state are installed\n");
}

/* AP state is separate from topology: discovered does not mean started. */
#define SMP_CPU_OFFLINE  0u
#define SMP_CPU_STARTING 1u
#define SMP_CPU_ONLINE   2u
static volatile uint32_t smp_cpu_state[SMP_MAX_CPUS];
static volatile uint32_t smp_online_count;

/*
 * Long-mode entry target for the future low-memory INIT/SIPI trampoline.
 * This intentionally parks APs after installing private descriptor state;
 * scheduler/process globals are not yet SMP-safe, so they must not schedule
 * user or kernel threads on these cores yet.
 */
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

    __asm__ volatile("" ::: "memory");
    smp_cpu_state[cpu_index] = SMP_CPU_ONLINE;
    __sync_fetch_and_add(&smp_online_count, 1);
    DbgPrint("smp: AP cpu%u (APIC ID=%u) initialized; parked pending SMP-safe scheduler\n",
             cpu_index, smp_topology.cpus[cpu_index].apic_id);

    for (;;) __asm__ volatile("cli; hlt");
}
