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
    out->xhci_context_size = xhci.context_size;
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
    out->xhci_port_reset_attempted = xhci.port_reset_attempted;
    out->xhci_port_reset_ok = xhci.port_reset_ok;
    out->xhci_port_reset_port = xhci.port_reset_port;
    out->xhci_port_reset_portsc_before = xhci.port_reset_portsc_before;
    out->xhci_port_reset_portsc_after = xhci.port_reset_portsc_after;
    out->xhci_port_reset_enabled = xhci.port_reset_enabled;
    out->xhci_port_reset_speed = xhci.port_reset_speed;
    out->xhci_port_reset_link_state = xhci.port_reset_link_state;
    out->xhci_address_device_attempted = xhci.address_device_attempted;
    out->xhci_address_device_ok = xhci.address_device_ok;
    out->xhci_address_device_completion_code = xhci.address_device_completion_code;
    out->xhci_address_device_slot_id = xhci.address_device_slot_id;
    out->xhci_address_device_event_type = xhci.address_device_event_type;
    out->xhci_address_device_event_trb0 = xhci.address_device_event_trb0;
    out->xhci_address_device_event_trb1 = xhci.address_device_event_trb1;
    out->xhci_address_device_event_trb2 = xhci.address_device_event_trb2;
    out->xhci_address_device_event_trb3 = xhci.address_device_event_trb3;
    out->xhci_input_context_phys = xhci.input_context_phys;
    out->xhci_output_context_phys = xhci.output_context_phys;
    out->xhci_ep0_ring_phys = xhci.ep0_ring_phys;
    out->xhci_ep0_max_packet_size = xhci.ep0_max_packet_size;
    out->xhci_erdp_after_address = xhci.erdp_after_address;
    out->xhci_addressed_device_address = xhci.addressed_device_address;
    out->xhci_addressed_slot_state = xhci.addressed_slot_state;
    out->xhci_output_slot_context0 = xhci.output_slot_context0;
    out->xhci_output_slot_context1 = xhci.output_slot_context1;
    out->xhci_output_slot_context2 = xhci.output_slot_context2;
    out->xhci_output_slot_context3 = xhci.output_slot_context3;
    out->xhci_device_descriptor_attempted = xhci.device_descriptor_attempted;
    out->xhci_device_descriptor_ok = xhci.device_descriptor_ok;
    out->xhci_device_descriptor_completion_code = xhci.device_descriptor_completion_code;
    out->xhci_device_descriptor_event_type = xhci.device_descriptor_event_type;
    out->xhci_device_descriptor_event_trb0 = xhci.device_descriptor_event_trb0;
    out->xhci_device_descriptor_event_trb1 = xhci.device_descriptor_event_trb1;
    out->xhci_device_descriptor_event_trb2 = xhci.device_descriptor_event_trb2;
    out->xhci_device_descriptor_event_trb3 = xhci.device_descriptor_event_trb3;
    out->xhci_device_descriptor_phys = xhci.device_descriptor_phys;
    out->xhci_device_descriptor_first8 = xhci.device_descriptor_first8;
    out->xhci_device_descriptor_second8 = xhci.device_descriptor_second8;
    out->xhci_device_descriptor_tail = xhci.device_descriptor_tail;
    out->xhci_device_descriptor_length = xhci.device_descriptor_length;
    out->xhci_device_descriptor_type = xhci.device_descriptor_type;
    out->xhci_device_usb_version = xhci.device_usb_version;
    out->xhci_device_class = xhci.device_class;
    out->xhci_device_subclass = xhci.device_subclass;
    out->xhci_device_protocol = xhci.device_protocol;
    out->xhci_device_max_packet_raw = xhci.device_max_packet_raw;
    out->xhci_device_vendor_id = xhci.device_vendor_id;
    out->xhci_device_product_id = xhci.device_product_id;
    out->xhci_device_version = xhci.device_version;
    out->xhci_configuration_descriptor_attempted = xhci.configuration_descriptor_attempted;
    out->xhci_configuration_descriptor_ok = xhci.configuration_descriptor_ok;
    out->xhci_configuration_descriptor_completion_code = xhci.configuration_descriptor_completion_code;
    out->xhci_configuration_descriptor_event_type = xhci.configuration_descriptor_event_type;
    out->xhci_configuration_descriptor_event_trb0 = xhci.configuration_descriptor_event_trb0;
    out->xhci_configuration_descriptor_event_trb1 = xhci.configuration_descriptor_event_trb1;
    out->xhci_configuration_descriptor_event_trb2 = xhci.configuration_descriptor_event_trb2;
    out->xhci_configuration_descriptor_event_trb3 = xhci.configuration_descriptor_event_trb3;
    out->xhci_configuration_descriptor_phys = xhci.configuration_descriptor_phys;
    out->xhci_configuration_descriptor_first8 = xhci.configuration_descriptor_first8;
    out->xhci_configuration_descriptor_second8 = xhci.configuration_descriptor_second8;
    out->xhci_configuration_descriptor_length = xhci.configuration_descriptor_length;
    out->xhci_configuration_descriptor_type = xhci.configuration_descriptor_type;
    out->xhci_configuration_total_length = xhci.configuration_total_length;
    out->xhci_configuration_num_interfaces = xhci.configuration_num_interfaces;
    out->xhci_configuration_value = xhci.configuration_value;
    out->xhci_configuration_attributes = xhci.configuration_attributes;
    out->xhci_configuration_max_power = xhci.configuration_max_power;
    out->xhci_first_interface_seen = xhci.first_interface_seen;
    out->xhci_first_interface_number = xhci.first_interface_number;
    out->xhci_first_interface_alternate = xhci.first_interface_alternate;
    out->xhci_first_interface_endpoint_count = xhci.first_interface_endpoint_count;
    out->xhci_first_interface_class = xhci.first_interface_class;
    out->xhci_first_interface_subclass = xhci.first_interface_subclass;
    out->xhci_first_interface_protocol = xhci.first_interface_protocol;
    out->xhci_endpoint_descriptor_count = xhci.endpoint_descriptor_count;
    out->xhci_first_bulk_in_seen = xhci.first_bulk_in_seen;
    out->xhci_first_bulk_in_address = xhci.first_bulk_in_address;
    out->xhci_first_bulk_in_attributes = xhci.first_bulk_in_attributes;
    out->xhci_first_bulk_in_max_packet_size = xhci.first_bulk_in_max_packet_size;
    out->xhci_first_bulk_in_interval = xhci.first_bulk_in_interval;
    out->xhci_first_bulk_out_seen = xhci.first_bulk_out_seen;
    out->xhci_first_bulk_out_address = xhci.first_bulk_out_address;
    out->xhci_first_bulk_out_attributes = xhci.first_bulk_out_attributes;
    out->xhci_first_bulk_out_max_packet_size = xhci.first_bulk_out_max_packet_size;
    out->xhci_first_bulk_out_interval = xhci.first_bulk_out_interval;
    out->xhci_first_bulk_in_dci = xhci.first_bulk_in_dci;
    out->xhci_first_bulk_out_dci = xhci.first_bulk_out_dci;
    out->xhci_configure_endpoint_attempted = xhci.configure_endpoint_attempted;
    out->xhci_configure_endpoint_ok = xhci.configure_endpoint_ok;
    out->xhci_configure_endpoint_completion_code = xhci.configure_endpoint_completion_code;
    out->xhci_configure_endpoint_slot_id = xhci.configure_endpoint_slot_id;
    out->xhci_configure_endpoint_event_type = xhci.configure_endpoint_event_type;
    out->xhci_configure_endpoint_event_trb0 = xhci.configure_endpoint_event_trb0;
    out->xhci_configure_endpoint_event_trb1 = xhci.configure_endpoint_event_trb1;
    out->xhci_configure_endpoint_event_trb2 = xhci.configure_endpoint_event_trb2;
    out->xhci_configure_endpoint_event_trb3 = xhci.configure_endpoint_event_trb3;
    out->xhci_configure_endpoint_input_context_phys = xhci.configure_endpoint_input_context_phys;
    out->xhci_bulk_in_ring_phys = xhci.bulk_in_ring_phys;
    out->xhci_bulk_out_ring_phys = xhci.bulk_out_ring_phys;
    out->xhci_bulk_in_endpoint_context0 = xhci.bulk_in_endpoint_context0;
    out->xhci_bulk_in_endpoint_context1 = xhci.bulk_in_endpoint_context1;
    out->xhci_bulk_in_endpoint_context2 = xhci.bulk_in_endpoint_context2;
    out->xhci_bulk_in_endpoint_context3 = xhci.bulk_in_endpoint_context3;
    out->xhci_bulk_in_endpoint_context4 = xhci.bulk_in_endpoint_context4;
    out->xhci_bulk_out_endpoint_context0 = xhci.bulk_out_endpoint_context0;
    out->xhci_bulk_out_endpoint_context1 = xhci.bulk_out_endpoint_context1;
    out->xhci_bulk_out_endpoint_context2 = xhci.bulk_out_endpoint_context2;
    out->xhci_bulk_out_endpoint_context3 = xhci.bulk_out_endpoint_context3;
    out->xhci_bulk_out_endpoint_context4 = xhci.bulk_out_endpoint_context4;
    out->xhci_set_configuration_attempted = xhci.set_configuration_attempted;
    out->xhci_set_configuration_ok = xhci.set_configuration_ok;
    out->xhci_set_configuration_value = xhci.set_configuration_value;
    out->xhci_set_configuration_completion_code = xhci.set_configuration_completion_code;
    out->xhci_set_configuration_event_type = xhci.set_configuration_event_type;
    out->xhci_set_configuration_event_trb0 = xhci.set_configuration_event_trb0;
    out->xhci_set_configuration_event_trb1 = xhci.set_configuration_event_trb1;
    out->xhci_set_configuration_event_trb2 = xhci.set_configuration_event_trb2;
    out->xhci_set_configuration_event_trb3 = xhci.set_configuration_event_trb3;
    out->xhci_bot_inquiry_attempted = xhci.bot_inquiry_attempted;
    out->xhci_bot_inquiry_ok = xhci.bot_inquiry_ok;
    out->xhci_bot_inquiry_tag = xhci.bot_inquiry_tag;
    out->xhci_bot_cbw_phys = xhci.bot_cbw_phys;
    out->xhci_bot_inquiry_data_phys = xhci.bot_inquiry_data_phys;
    out->xhci_bot_csw_phys = xhci.bot_csw_phys;
    out->xhci_bot_cbw_completion_code = xhci.bot_cbw_completion_code;
    out->xhci_bot_cbw_event_type = xhci.bot_cbw_event_type;
    out->xhci_bot_cbw_event_trb0 = xhci.bot_cbw_event_trb0;
    out->xhci_bot_cbw_event_trb1 = xhci.bot_cbw_event_trb1;
    out->xhci_bot_cbw_event_trb2 = xhci.bot_cbw_event_trb2;
    out->xhci_bot_cbw_event_trb3 = xhci.bot_cbw_event_trb3;
    out->xhci_bot_data_completion_code = xhci.bot_data_completion_code;
    out->xhci_bot_data_event_type = xhci.bot_data_event_type;
    out->xhci_bot_data_event_trb0 = xhci.bot_data_event_trb0;
    out->xhci_bot_data_event_trb1 = xhci.bot_data_event_trb1;
    out->xhci_bot_data_event_trb2 = xhci.bot_data_event_trb2;
    out->xhci_bot_data_event_trb3 = xhci.bot_data_event_trb3;
    out->xhci_bot_csw_completion_code = xhci.bot_csw_completion_code;
    out->xhci_bot_csw_event_type = xhci.bot_csw_event_type;
    out->xhci_bot_csw_event_trb0 = xhci.bot_csw_event_trb0;
    out->xhci_bot_csw_event_trb1 = xhci.bot_csw_event_trb1;
    out->xhci_bot_csw_event_trb2 = xhci.bot_csw_event_trb2;
    out->xhci_bot_csw_event_trb3 = xhci.bot_csw_event_trb3;
    out->xhci_bot_csw_signature = xhci.bot_csw_signature;
    out->xhci_bot_csw_tag = xhci.bot_csw_tag;
    out->xhci_bot_csw_residue = xhci.bot_csw_residue;
    out->xhci_bot_csw_status = xhci.bot_csw_status;
    out->xhci_inquiry_peripheral = xhci.inquiry_peripheral;
    out->xhci_inquiry_removable = xhci.inquiry_removable;
    out->xhci_inquiry_version = xhci.inquiry_version;
    out->xhci_inquiry_response_format = xhci.inquiry_response_format;
    out->xhci_inquiry_additional_length = xhci.inquiry_additional_length;
    out->xhci_inquiry_vendor_first8 = xhci.inquiry_vendor_first8;
    out->xhci_inquiry_product_first8 = xhci.inquiry_product_first8;
    out->xhci_inquiry_product_second8 = xhci.inquiry_product_second8;
    out->xhci_inquiry_revision = xhci.inquiry_revision;
    out->xhci_bot_capacity_attempted = xhci.bot_capacity_attempted;
    out->xhci_bot_capacity_ok = xhci.bot_capacity_ok;
    out->xhci_bot_capacity_tag = xhci.bot_capacity_tag;
    out->xhci_bot_capacity_data_phys = xhci.bot_capacity_data_phys;
    out->xhci_bot_capacity_cbw_completion_code = xhci.bot_capacity_cbw_completion_code;
    out->xhci_bot_capacity_data_completion_code = xhci.bot_capacity_data_completion_code;
    out->xhci_bot_capacity_csw_completion_code = xhci.bot_capacity_csw_completion_code;
    out->xhci_bot_capacity_csw_signature = xhci.bot_capacity_csw_signature;
    out->xhci_bot_capacity_csw_tag = xhci.bot_capacity_csw_tag;
    out->xhci_bot_capacity_csw_residue = xhci.bot_capacity_csw_residue;
    out->xhci_bot_capacity_csw_status = xhci.bot_capacity_csw_status;
    out->xhci_bot_capacity_last_lba = xhci.bot_capacity_last_lba;
    out->xhci_bot_capacity_block_size = xhci.bot_capacity_block_size;
    out->xhci_bot_capacity_block_count = xhci.bot_capacity_block_count;
    out->xhci_bot_read10_attempted = xhci.bot_read10_attempted;
    out->xhci_bot_read10_ok = xhci.bot_read10_ok;
    out->xhci_bot_read10_tag = xhci.bot_read10_tag;
    out->xhci_bot_read10_lba = xhci.bot_read10_lba;
    out->xhci_bot_read10_bytes = xhci.bot_read10_bytes;
    out->xhci_bot_read10_data_phys = xhci.bot_read10_data_phys;
    out->xhci_bot_read10_cbw_completion_code = xhci.bot_read10_cbw_completion_code;
    out->xhci_bot_read10_data_completion_code = xhci.bot_read10_data_completion_code;
    out->xhci_bot_read10_csw_completion_code = xhci.bot_read10_csw_completion_code;
    out->xhci_bot_read10_csw_signature = xhci.bot_read10_csw_signature;
    out->xhci_bot_read10_csw_tag = xhci.bot_read10_csw_tag;
    out->xhci_bot_read10_csw_residue = xhci.bot_read10_csw_residue;
    out->xhci_bot_read10_csw_status = xhci.bot_read10_csw_status;
    out->xhci_bot_read10_first8 = xhci.bot_read10_first8;
    out->xhci_bot_read10_second8 = xhci.bot_read10_second8;
    out->xhci_bot_write10_attempted = xhci.bot_write10_attempted;
    out->xhci_bot_write10_ok = xhci.bot_write10_ok;
    out->xhci_bot_write10_verified = xhci.bot_write10_verified;
    out->xhci_bot_write10_tag = xhci.bot_write10_tag;
    out->xhci_bot_write10_lba = xhci.bot_write10_lba;
    out->xhci_bot_write10_bytes = xhci.bot_write10_bytes;
    out->xhci_bot_write10_data_phys = xhci.bot_write10_data_phys;
    out->xhci_bot_write10_cbw_completion_code = xhci.bot_write10_cbw_completion_code;
    out->xhci_bot_write10_data_completion_code = xhci.bot_write10_data_completion_code;
    out->xhci_bot_write10_csw_completion_code = xhci.bot_write10_csw_completion_code;
    out->xhci_bot_write10_csw_signature = xhci.bot_write10_csw_signature;
    out->xhci_bot_write10_csw_tag = xhci.bot_write10_csw_tag;
    out->xhci_bot_write10_csw_residue = xhci.bot_write10_csw_residue;
    out->xhci_bot_write10_csw_status = xhci.bot_write10_csw_status;
    out->xhci_bot_write10_first8 = xhci.bot_write10_first8;
    out->xhci_bot_write10_second8 = xhci.bot_write10_second8;
}
