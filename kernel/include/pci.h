#ifndef TANGPINGOS_PCI_H
#define TANGPINGOS_PCI_H

#include <stdint.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
};

struct pci_xhci_info {
    uint64_t found;
    uint64_t count;
    uint64_t bus;
    uint64_t slot;
    uint64_t function;
    uint64_t vendor_id;
    uint64_t device_id;
    uint64_t revision;
    uint64_t irq_line;
    uint64_t bar0_raw;
    uint64_t bar0_mmio_base;
    uint64_t bar0_is_64bit;
    uint64_t mmio_mapped;
    uint64_t cap_length;
    uint64_t hci_version;
    uint64_t max_slots;
    uint64_t max_interrupters;
    uint64_t max_ports;
    uint64_t doorbell_offset;
    uint64_t runtime_offset;
    uint64_t op_regs_ready;
    uint64_t op_usbcmd_before;
    uint64_t op_usbsts_before;
    uint64_t op_pagesize;
    uint64_t reset_allowed;
    uint64_t reset_attempted;
    uint64_t reset_ok;
    uint64_t halt_ok;
    uint64_t ready_ok;
    uint64_t op_usbcmd_after;
    uint64_t op_usbsts_after;
    uint64_t ext_cap_offset;
    uint64_t ext_cap_count;
    uint64_t legacy_cap_offset;
    uint64_t legacy_bios_owned_before;
    uint64_t legacy_os_owned_before;
    uint64_t legacy_bios_owned_after;
    uint64_t legacy_os_owned_after;
    uint64_t handoff_attempted;
    uint64_t handoff_ok;
    uint64_t rings_ready;
    uint64_t command_ring_phys;
    uint64_t event_ring_phys;
    uint64_t erst_phys;
    uint64_t dcbaa_phys;
    uint64_t scratchpad_count;
    uint64_t scratchpad_array_phys;
    uint64_t crcr_value;
    uint64_t dcbaap_value;
    uint64_t erstsz_value;
    uint64_t erstba_value;
    uint64_t erdp_value;
    uint64_t controller_started;
    uint64_t config_value;
    uint64_t usbcmd_run;
    uint64_t usbsts_run;
    uint64_t enable_slot_attempted;
    uint64_t enable_slot_ok;
    uint64_t enable_slot_completion_code;
    uint64_t enable_slot_id;
    uint64_t enable_slot_event_type;
    uint64_t enable_slot_event_trb0;
    uint64_t enable_slot_event_trb1;
    uint64_t enable_slot_event_trb2;
    uint64_t enable_slot_event_trb3;
    uint64_t command_doorbell_value;
    uint64_t erdp_after_command;
    uint64_t port_scan_done;
    uint64_t port_scan_limit;
    uint64_t connected_port_count;
    uint64_t enabled_port_count;
    uint64_t powered_port_count;
    uint64_t first_connected_port;
    uint64_t first_connected_portsc;
    uint64_t first_connected_speed;
    uint64_t first_connected_enabled;
    uint64_t first_connected_powered;
    uint64_t first_connected_link_state;
    uint64_t port1_portsc;
};

void pci_init(void);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out);
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_device *out);
void pci_get_xhci_info(struct pci_xhci_info *out);
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_bar(const struct pci_device *device, uint8_t bar_index);
void pci_enable_io_bus_master(const struct pci_device *device);
void pci_enable_mmio_bus_master(const struct pci_device *device);

#endif
