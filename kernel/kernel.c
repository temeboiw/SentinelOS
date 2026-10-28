#include "bootinfo.h"
#include "klib.h"
#include "serial.h"
#include "cpu.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "ke.h"
#include "bootcheck.h"
#include "virtio_blk.h"
#include "hda.h"
#include "smp.h"
#include "pci_network.h"
#include "e1000.h"
#include "netstack.h"
#include "driver.h"
#include "iomgr.h"
#include "driver_loader.h"
#include "user.h"

/* MSVC/clang stack probe ºw^~)Þw we are freestanding */
__attribute__((naked)) void __chkstk(void) {
    __asm__ volatile("ret");
}


// Edit and rebuild to exercise the exception handler:
//   0 = normal boot   1 = #UD (ud2)   2 = #GP (non-canonical access)   3 = #PF (unmapped address)
#ifndef TEST_FAULT
#define TEST_FAULT 0
#endif

static uint8_t kstack[64 * 1024] __attribute__((aligned(16)));

__attribute__((used, noreturn)) void kmain2(BootInfo *bi) {
    HalInitSystem(0, bi);
    DbgPrint("\n=== magic kernel ===\n");
    DbgPrint("bootinfo: fb=%p %ux%u pitch=%u  mmap=%p size=%llu desc=%llu (%llu entries)\n",
            (void *)bi->fb_base, bi->width, bi->height, bi->pitch,
            (void *)bi->mmap, bi->mmap_size, bi->desc_size,
            bi->desc_size ? bi->mmap_size / bi->desc_size : 0);

    cpu_init();
    PciScanNetworkDevices();
    uint16_t cs; uint64_t sp;
    __asm__ volatile("movw %%cs, %0" : "=r"(cs));
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
    DbgPrint("gdt+idt loaded: cs=%04x rsp=%p (kstack %p..%p)\n",
            cs, (void *)sp, (void *)kstack, (void *)(kstack + sizeof kstack));

    DbgPrint("testing int3...\n");
    __asm__ volatile("int3");
    DbgPrint("survived int3\n");

    DbgPrint("\n-- physical memory --\n");
    mmap_dump(bi);
    pmm_init(bi);
    DbgPrint("pmm: %llu of %llu pages free (%llu MiB free)\n",
            pmm_free_pages(), pmm_total_pages(), pmm_free_pages() * 4 / 1024);
    pmm_selftest((uint64_t)kmain2, (uint64_t)kstack);

    DbgPrint("\n-- virtual memory --\n");
    vmm_init(bi);
    if (E1000Init()) NetInit();
    smp_discover();
    hda_init();
    vmm_selftest(bi, (uint64_t)kmain2, (uint64_t)kstack);
    uint64_t got = pmm_reclaim_boot_services(bi);
    DbgPrint("pmm: reclaimed %llu pages (%llu MiB) of boot-services memory, %llu MiB free now\n",
            got, got * 4 / 1024, pmm_free_pages() * 4 / 1024);
    pmm_touch_test(HHDM_BASE);

    DbgPrint("\n-- kernel heap --\n");
    heap_selftest();
    heap_stats();
    ObInitialize();
    IoInitialize();
    int ob_ok = ObSelfTest();
    int io_ok = IoSelfTest();
    DbgPrint("object manager: namespace/handles/access checks %s (objects=%u handles=%u)\n", ob_ok ? "ok" : "FAILED", ObGetObjectCount(), ObGetHandleCount());
    DbgPrint("I/O manager: device/IRP dispatch %s\n", io_ok ? "ok" : "FAILED");
    SentinelDriverManagerInitialize();
    DbgPrint("driver manager: ABI v%u registry ready (%u slots); .sys image and filesystem loader ready\n",
             SENTINEL_DRIVER_ABI_VERSION, SENTINEL_DRIVER_REGISTRY_MAX);
    modules_dump(bi); hal_selftest(); scheduler_selftest(bi);
    smp_start_aps(bi);

#if TEST_FAULT == 1
    DbgPrint("triggering #UD...\n");
    __asm__ volatile("ud2");
#elif TEST_FAULT == 3
    DbgPrint("triggering #PF...\n");
    volatile uint64_t *unmapped = (volatile uint64_t *)0x00007F0000000000ULL;
    (void)*unmapped;
#elif TEST_FAULT == 4
    DbgPrint("triggering a double fault (stack pointer = 0, then int3)...\n");
    __asm__ volatile("movq $0, %%rsp\n\tint3" ::: "memory");
#elif TEST_FAULT == 2
    DbgPrint("triggering #GP...\n");
    volatile uint64_t *bad = (volatile uint64_t *)0x1234000000000000ULL;   // non-canonical
    (void)*bad;
#endif

    // ---- usermode demo ----
    user_enter(bi);   // does not return on success (exit syscall ºw^~)Þv halt)

    /* Usermode returned"éÝyø§yÓ leave the framebuffer alone (taskbar stays). */
    DbgPrint("kernel: idle\n");
    for (;;) __asm__ volatile("sti; hlt");
}

// entry point: interrupts off, move to our own stack, continue in kmain2
void kmain(BootInfo *bi) {
    __asm__ volatile("cli");
    uint64_t top = (uint64_t)kstack + sizeof kstack;
    __asm__ volatile(
        "movq %%rax, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "call kmain2\n\t"
        :: "a"(top), "c"(bi) : "memory");
    __builtin_unreachable();
}
