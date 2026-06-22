#include <memory.h>
#include <pci.h>
#include <scheduler.h>
#include <stdint.h>
#include <sysinfo.h>

#define PIT_HZ 100ULL

static struct sys_system_info boot_info;

static void zero_bytes(void *ptr, uint64_t len) {
    uint8_t *out = ptr;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = 0;
    }
}

static void copy_bytes(char *dst, const char *src, uint64_t max_len) {
    uint64_t i = 0;
    if (max_len == 0) {
        return;
    }
    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_raw(void *dst, const void *src, uint64_t len) {
    uint8_t *out = dst;
    const uint8_t *in = src;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = in[i];
    }
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static void detect_cpu_name(char *out, uint64_t out_len) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_extended;

    if (out_len == 0) {
        return;
    }

    cpuid(0x80000000U, &max_extended, &ebx, &ecx, &edx);
    if (max_extended >= 0x80000004U && out_len >= 49) {
        uint32_t *brand = (uint32_t *)out;
        for (uint32_t leaf = 0; leaf < 3; leaf++) {
            cpuid(0x80000002U + leaf,
                  &brand[leaf * 4 + 0],
                  &brand[leaf * 4 + 1],
                  &brand[leaf * 4 + 2],
                  &brand[leaf * 4 + 3]);
        }
        out[48] = '\0';
        return;
    }

    cpuid(0, &eax, &ebx, &ecx, &edx);
    char vendor[13];
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = '\0';
    copy_bytes(out, vendor, out_len);
}

void sysinfo_init(struct limine_framebuffer *framebuffer, struct limine_memmap_response *memmap) {
    zero_bytes(&boot_info, sizeof(boot_info));

    if (framebuffer != 0) {
        boot_info.framebuffer_width = framebuffer->width;
        boot_info.framebuffer_height = framebuffer->height;
        boot_info.framebuffer_pitch = framebuffer->pitch;
        boot_info.framebuffer_bpp = framebuffer->bpp;
    }
    if (memmap != 0) {
        boot_info.memmap_entries = memmap->entry_count;
    }
    boot_info.page_size = PAGE_SIZE;
    boot_info.timer_hz = PIT_HZ;
    detect_cpu_name(boot_info.cpu_name, sizeof(boot_info.cpu_name));
}

void sysinfo_get_meminfo(struct sys_meminfo *out) {
    if (out == 0) {
        return;
    }

    out->total_pages = pmm_total_pages();
    out->free_pages = pmm_free_pages();
    out->used_pages = out->total_pages - out->free_pages;
    out->page_size = PAGE_SIZE;
}

void sysinfo_get_system_info(struct sys_system_info *out) {
    struct pci_xhci_info xhci;

    if (out == 0) {
        return;
    }

    copy_raw(out, &boot_info, sizeof(*out));
    out->total_pages = pmm_total_pages();
    out->free_pages = pmm_free_pages();
    out->used_pages = out->total_pages - out->free_pages;
    out->ticks = scheduler_ticks();
    pci_get_xhci_info(&xhci);
    out->xhci_found = xhci.found;
    out->xhci_count = xhci.count;
    out->xhci_bus = xhci.bus;
    out->xhci_slot = xhci.slot;
    out->xhci_function = xhci.function;
    out->xhci_vendor_id = xhci.vendor_id;
    out->xhci_device_id = xhci.device_id;
    out->xhci_revision = xhci.revision;
    out->xhci_irq_line = xhci.irq_line;
    out->xhci_bar0_raw = xhci.bar0_raw;
    out->xhci_mmio_base = xhci.bar0_mmio_base;
    out->xhci_bar0_is_64bit = xhci.bar0_is_64bit;
    out->xhci_mmio_mapped = xhci.mmio_mapped;
    out->xhci_cap_length = xhci.cap_length;
    out->xhci_hci_version = xhci.hci_version;
    out->xhci_max_slots = xhci.max_slots;
    out->xhci_max_interrupters = xhci.max_interrupters;
    out->xhci_max_ports = xhci.max_ports;
    out->xhci_doorbell_offset = xhci.doorbell_offset;
    out->xhci_runtime_offset = xhci.runtime_offset;
    out->xhci_op_regs_ready = xhci.op_regs_ready;
    out->xhci_op_usbcmd_before = xhci.op_usbcmd_before;
    out->xhci_op_usbsts_before = xhci.op_usbsts_before;
    out->xhci_op_pagesize = xhci.op_pagesize;
    out->xhci_reset_allowed = xhci.reset_allowed;
    out->xhci_reset_attempted = xhci.reset_attempted;
    out->xhci_reset_ok = xhci.reset_ok;
    out->xhci_halt_ok = xhci.halt_ok;
    out->xhci_ready_ok = xhci.ready_ok;
    out->xhci_op_usbcmd_after = xhci.op_usbcmd_after;
    out->xhci_op_usbsts_after = xhci.op_usbsts_after;
    out->xhci_ext_cap_offset = xhci.ext_cap_offset;
    out->xhci_ext_cap_count = xhci.ext_cap_count;
    out->xhci_legacy_cap_offset = xhci.legacy_cap_offset;
    out->xhci_legacy_bios_owned_before = xhci.legacy_bios_owned_before;
    out->xhci_legacy_os_owned_before = xhci.legacy_os_owned_before;
    out->xhci_legacy_bios_owned_after = xhci.legacy_bios_owned_after;
    out->xhci_legacy_os_owned_after = xhci.legacy_os_owned_after;
    out->xhci_handoff_attempted = xhci.handoff_attempted;
    out->xhci_handoff_ok = xhci.handoff_ok;
    out->xhci_rings_ready = xhci.rings_ready;
    out->xhci_command_ring_phys = xhci.command_ring_phys;
    out->xhci_event_ring_phys = xhci.event_ring_phys;
    out->xhci_erst_phys = xhci.erst_phys;
    out->xhci_dcbaa_phys = xhci.dcbaa_phys;
    out->xhci_scratchpad_count = xhci.scratchpad_count;
    out->xhci_scratchpad_array_phys = xhci.scratchpad_array_phys;
    out->xhci_crcr_value = xhci.crcr_value;
    out->xhci_dcbaap_value = xhci.dcbaap_value;
    out->xhci_erstsz_value = xhci.erstsz_value;
    out->xhci_erstba_value = xhci.erstba_value;
    out->xhci_erdp_value = xhci.erdp_value;
    out->xhci_controller_started = xhci.controller_started;
    out->xhci_config_value = xhci.config_value;
    out->xhci_usbcmd_run = xhci.usbcmd_run;
    out->xhci_usbsts_run = xhci.usbsts_run;
    out->xhci_enable_slot_attempted = xhci.enable_slot_attempted;
    out->xhci_enable_slot_ok = xhci.enable_slot_ok;
    out->xhci_enable_slot_completion_code = xhci.enable_slot_completion_code;
    out->xhci_enable_slot_id = xhci.enable_slot_id;
    out->xhci_enable_slot_event_type = xhci.enable_slot_event_type;
    out->xhci_enable_slot_event_trb0 = xhci.enable_slot_event_trb0;
    out->xhci_enable_slot_event_trb1 = xhci.enable_slot_event_trb1;
    out->xhci_enable_slot_event_trb2 = xhci.enable_slot_event_trb2;
    out->xhci_enable_slot_event_trb3 = xhci.enable_slot_event_trb3;
    out->xhci_command_doorbell_value = xhci.command_doorbell_value;
    out->xhci_erdp_after_command = xhci.erdp_after_command;
    out->xhci_port_scan_done = xhci.port_scan_done;
    out->xhci_port_scan_limit = xhci.port_scan_limit;
    out->xhci_connected_port_count = xhci.connected_port_count;
    out->xhci_enabled_port_count = xhci.enabled_port_count;
    out->xhci_powered_port_count = xhci.powered_port_count;
    out->xhci_first_connected_port = xhci.first_connected_port;
    out->xhci_first_connected_portsc = xhci.first_connected_portsc;
    out->xhci_first_connected_speed = xhci.first_connected_speed;
    out->xhci_first_connected_enabled = xhci.first_connected_enabled;
    out->xhci_first_connected_powered = xhci.first_connected_powered;
    out->xhci_first_connected_link_state = xhci.first_connected_link_state;
    out->xhci_port1_portsc = xhci.port1_portsc;
}
