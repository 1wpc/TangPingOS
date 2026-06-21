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
};

void pci_init(void);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out);
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_bar(const struct pci_device *device, uint8_t bar_index);
void pci_enable_io_bus_master(const struct pci_device *device);

#endif
