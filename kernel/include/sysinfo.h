#ifndef TANGPINGOS_SYSINFO_H
#define TANGPINGOS_SYSINFO_H

#include <limine.h>
#include <stdint.h>

#define SYSINFO_CPU_NAME_MAX 64

struct sys_meminfo {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
};

struct sys_system_info {
    uint64_t framebuffer_width;
    uint64_t framebuffer_height;
    uint64_t framebuffer_pitch;
    uint64_t framebuffer_bpp;
    uint64_t memmap_entries;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
    uint64_t ticks;
    uint64_t timer_hz;
    uint64_t xhci_found;
    uint64_t xhci_count;
    uint64_t xhci_bus;
    uint64_t xhci_slot;
    uint64_t xhci_function;
    uint64_t xhci_vendor_id;
    uint64_t xhci_device_id;
    uint64_t xhci_revision;
    uint64_t xhci_irq_line;
    uint64_t xhci_bar0_raw;
    uint64_t xhci_mmio_base;
    uint64_t xhci_bar0_is_64bit;
    uint64_t xhci_mmio_mapped;
    uint64_t xhci_cap_length;
    uint64_t xhci_hci_version;
    uint64_t xhci_max_slots;
    uint64_t xhci_max_interrupters;
    uint64_t xhci_max_ports;
    uint64_t xhci_doorbell_offset;
    uint64_t xhci_runtime_offset;
    uint64_t xhci_op_regs_ready;
    uint64_t xhci_op_usbcmd_before;
    uint64_t xhci_op_usbsts_before;
    uint64_t xhci_op_pagesize;
    uint64_t xhci_reset_allowed;
    uint64_t xhci_reset_attempted;
    uint64_t xhci_reset_ok;
    uint64_t xhci_halt_ok;
    uint64_t xhci_ready_ok;
    uint64_t xhci_op_usbcmd_after;
    uint64_t xhci_op_usbsts_after;
    uint64_t xhci_ext_cap_offset;
    uint64_t xhci_ext_cap_count;
    uint64_t xhci_legacy_cap_offset;
    uint64_t xhci_legacy_bios_owned_before;
    uint64_t xhci_legacy_os_owned_before;
    uint64_t xhci_legacy_bios_owned_after;
    uint64_t xhci_legacy_os_owned_after;
    uint64_t xhci_handoff_attempted;
    uint64_t xhci_handoff_ok;
    uint64_t xhci_rings_ready;
    uint64_t xhci_command_ring_phys;
    uint64_t xhci_event_ring_phys;
    uint64_t xhci_erst_phys;
    uint64_t xhci_dcbaa_phys;
    uint64_t xhci_scratchpad_count;
    uint64_t xhci_scratchpad_array_phys;
    uint64_t xhci_crcr_value;
    uint64_t xhci_dcbaap_value;
    uint64_t xhci_erstsz_value;
    uint64_t xhci_erstba_value;
    uint64_t xhci_erdp_value;
    uint64_t xhci_controller_started;
    uint64_t xhci_config_value;
    uint64_t xhci_usbcmd_run;
    uint64_t xhci_usbsts_run;
    uint64_t xhci_enable_slot_attempted;
    uint64_t xhci_enable_slot_ok;
    uint64_t xhci_enable_slot_completion_code;
    uint64_t xhci_enable_slot_id;
    uint64_t xhci_enable_slot_event_type;
    uint64_t xhci_enable_slot_event_trb0;
    uint64_t xhci_enable_slot_event_trb1;
    uint64_t xhci_enable_slot_event_trb2;
    uint64_t xhci_enable_slot_event_trb3;
    uint64_t xhci_command_doorbell_value;
    uint64_t xhci_erdp_after_command;
    uint64_t xhci_port_scan_done;
    uint64_t xhci_port_scan_limit;
    uint64_t xhci_connected_port_count;
    uint64_t xhci_enabled_port_count;
    uint64_t xhci_powered_port_count;
    uint64_t xhci_first_connected_port;
    uint64_t xhci_first_connected_portsc;
    uint64_t xhci_first_connected_speed;
    uint64_t xhci_first_connected_enabled;
    uint64_t xhci_first_connected_powered;
    uint64_t xhci_first_connected_link_state;
    uint64_t xhci_port1_portsc;
    char cpu_name[SYSINFO_CPU_NAME_MAX];
};

void sysinfo_init(struct limine_framebuffer *framebuffer, struct limine_memmap_response *memmap);
void sysinfo_get_meminfo(struct sys_meminfo *out);
void sysinfo_get_system_info(struct sys_system_info *out);

#endif
