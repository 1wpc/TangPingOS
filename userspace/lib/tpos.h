#ifndef TPOS_H
#define TPOS_H

#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_GETPID 3
#define SYS_YIELD  4
#define SYS_SLEEP_TICKS 5
#define SYS_BRK 6
#define SYS_OPEN 8
#define SYS_READ 9
#define SYS_CLOSE 10
#define SYS_GETDENTS 11
#define SYS_DUP2 13
#define SYS_LSEEK 15
#define SYS_UNLINK 16
#define SYS_SPAWN 17
#define SYS_TASK_INFO 18
#define SYS_MEMINFO 19
#define SYS_SYSINFO 20
#define SYS_UPTIME 21
#define SYS_BLOCK_INFO 22
#define SYS_BLOCK_READ 23
#define SYS_BLOCK_WRITE 24
#define SYS_MOUNT_INFO 25

#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2

#define DIRENT_NAME_MAX 64
#define TASK_INFO_NAME_MAX 32
#define SYSINFO_CPU_NAME_MAX 64
#define BLOCK_DEVICE_NAME_MAX 32
#define MOUNT_NAME_MAX 32
#define MOUNT_PATH_MAX 64
#define MOUNT_SOURCE_MAX 64

#define DIRENT_TYPE_FILE 1
#define DIRENT_TYPE_DIR 2
#define DIRENT_TYPE_DEVICE 3

#define OPEN_CREATE 1ULL
#define OPEN_TRUNC  2ULL
#define OPEN_APPEND 4ULL

#define SEEK_SET 0ULL
#define SEEK_CUR 1ULL
#define SEEK_END 2ULL

struct dirent {
    char name[DIRENT_NAME_MAX];
    uint64_t type;
    uint64_t size;
};

struct task_info {
    uint64_t pid;
    uint64_t state;
    uint64_t exit_status;
    uint64_t switches;
    char name[TASK_INFO_NAME_MAX];
};

struct meminfo {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t page_size;
};

struct system_info {
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
    uint64_t xhci_context_size;
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
    uint64_t xhci_port_reset_attempted;
    uint64_t xhci_port_reset_ok;
    uint64_t xhci_port_reset_port;
    uint64_t xhci_port_reset_portsc_before;
    uint64_t xhci_port_reset_portsc_after;
    uint64_t xhci_port_reset_enabled;
    uint64_t xhci_port_reset_speed;
    uint64_t xhci_port_reset_link_state;
    uint64_t xhci_address_device_attempted;
    uint64_t xhci_address_device_ok;
    uint64_t xhci_address_device_completion_code;
    uint64_t xhci_address_device_slot_id;
    uint64_t xhci_address_device_event_type;
    uint64_t xhci_address_device_event_trb0;
    uint64_t xhci_address_device_event_trb1;
    uint64_t xhci_address_device_event_trb2;
    uint64_t xhci_address_device_event_trb3;
    uint64_t xhci_input_context_phys;
    uint64_t xhci_output_context_phys;
    uint64_t xhci_ep0_ring_phys;
    uint64_t xhci_ep0_max_packet_size;
    uint64_t xhci_erdp_after_address;
    uint64_t xhci_addressed_device_address;
    uint64_t xhci_addressed_slot_state;
    uint64_t xhci_output_slot_context0;
    uint64_t xhci_output_slot_context1;
    uint64_t xhci_output_slot_context2;
    uint64_t xhci_output_slot_context3;
    uint64_t xhci_device_descriptor_attempted;
    uint64_t xhci_device_descriptor_ok;
    uint64_t xhci_device_descriptor_completion_code;
    uint64_t xhci_device_descriptor_event_type;
    uint64_t xhci_device_descriptor_event_trb0;
    uint64_t xhci_device_descriptor_event_trb1;
    uint64_t xhci_device_descriptor_event_trb2;
    uint64_t xhci_device_descriptor_event_trb3;
    uint64_t xhci_device_descriptor_phys;
    uint64_t xhci_device_descriptor_first8;
    uint64_t xhci_device_descriptor_second8;
    uint64_t xhci_device_descriptor_tail;
    uint64_t xhci_device_descriptor_length;
    uint64_t xhci_device_descriptor_type;
    uint64_t xhci_device_usb_version;
    uint64_t xhci_device_class;
    uint64_t xhci_device_subclass;
    uint64_t xhci_device_protocol;
    uint64_t xhci_device_max_packet_raw;
    uint64_t xhci_device_vendor_id;
    uint64_t xhci_device_product_id;
    uint64_t xhci_device_version;
    uint64_t xhci_configuration_descriptor_attempted;
    uint64_t xhci_configuration_descriptor_ok;
    uint64_t xhci_configuration_descriptor_completion_code;
    uint64_t xhci_configuration_descriptor_event_type;
    uint64_t xhci_configuration_descriptor_event_trb0;
    uint64_t xhci_configuration_descriptor_event_trb1;
    uint64_t xhci_configuration_descriptor_event_trb2;
    uint64_t xhci_configuration_descriptor_event_trb3;
    uint64_t xhci_configuration_descriptor_phys;
    uint64_t xhci_configuration_descriptor_first8;
    uint64_t xhci_configuration_descriptor_second8;
    uint64_t xhci_configuration_descriptor_length;
    uint64_t xhci_configuration_descriptor_type;
    uint64_t xhci_configuration_total_length;
    uint64_t xhci_configuration_num_interfaces;
    uint64_t xhci_configuration_value;
    uint64_t xhci_configuration_attributes;
    uint64_t xhci_configuration_max_power;
    uint64_t xhci_first_interface_seen;
    uint64_t xhci_first_interface_number;
    uint64_t xhci_first_interface_alternate;
    uint64_t xhci_first_interface_endpoint_count;
    uint64_t xhci_first_interface_class;
    uint64_t xhci_first_interface_subclass;
    uint64_t xhci_first_interface_protocol;
    uint64_t xhci_endpoint_descriptor_count;
    uint64_t xhci_first_bulk_in_seen;
    uint64_t xhci_first_bulk_in_address;
    uint64_t xhci_first_bulk_in_attributes;
    uint64_t xhci_first_bulk_in_max_packet_size;
    uint64_t xhci_first_bulk_in_interval;
    uint64_t xhci_first_bulk_out_seen;
    uint64_t xhci_first_bulk_out_address;
    uint64_t xhci_first_bulk_out_attributes;
    uint64_t xhci_first_bulk_out_max_packet_size;
    uint64_t xhci_first_bulk_out_interval;
    uint64_t xhci_first_bulk_in_dci;
    uint64_t xhci_first_bulk_out_dci;
    uint64_t xhci_configure_endpoint_attempted;
    uint64_t xhci_configure_endpoint_ok;
    uint64_t xhci_configure_endpoint_completion_code;
    uint64_t xhci_configure_endpoint_slot_id;
    uint64_t xhci_configure_endpoint_event_type;
    uint64_t xhci_configure_endpoint_event_trb0;
    uint64_t xhci_configure_endpoint_event_trb1;
    uint64_t xhci_configure_endpoint_event_trb2;
    uint64_t xhci_configure_endpoint_event_trb3;
    uint64_t xhci_configure_endpoint_input_context_phys;
    uint64_t xhci_bulk_in_ring_phys;
    uint64_t xhci_bulk_out_ring_phys;
    uint64_t xhci_bulk_in_endpoint_context0;
    uint64_t xhci_bulk_in_endpoint_context1;
    uint64_t xhci_bulk_in_endpoint_context2;
    uint64_t xhci_bulk_in_endpoint_context3;
    uint64_t xhci_bulk_in_endpoint_context4;
    uint64_t xhci_bulk_out_endpoint_context0;
    uint64_t xhci_bulk_out_endpoint_context1;
    uint64_t xhci_bulk_out_endpoint_context2;
    uint64_t xhci_bulk_out_endpoint_context3;
    uint64_t xhci_bulk_out_endpoint_context4;
    uint64_t xhci_set_configuration_attempted;
    uint64_t xhci_set_configuration_ok;
    uint64_t xhci_set_configuration_value;
    uint64_t xhci_set_configuration_completion_code;
    uint64_t xhci_set_configuration_event_type;
    uint64_t xhci_set_configuration_event_trb0;
    uint64_t xhci_set_configuration_event_trb1;
    uint64_t xhci_set_configuration_event_trb2;
    uint64_t xhci_set_configuration_event_trb3;
    uint64_t xhci_bot_inquiry_attempted;
    uint64_t xhci_bot_inquiry_ok;
    uint64_t xhci_bot_inquiry_tag;
    uint64_t xhci_bot_cbw_phys;
    uint64_t xhci_bot_inquiry_data_phys;
    uint64_t xhci_bot_csw_phys;
    uint64_t xhci_bot_cbw_completion_code;
    uint64_t xhci_bot_cbw_event_type;
    uint64_t xhci_bot_cbw_event_trb0;
    uint64_t xhci_bot_cbw_event_trb1;
    uint64_t xhci_bot_cbw_event_trb2;
    uint64_t xhci_bot_cbw_event_trb3;
    uint64_t xhci_bot_data_completion_code;
    uint64_t xhci_bot_data_event_type;
    uint64_t xhci_bot_data_event_trb0;
    uint64_t xhci_bot_data_event_trb1;
    uint64_t xhci_bot_data_event_trb2;
    uint64_t xhci_bot_data_event_trb3;
    uint64_t xhci_bot_csw_completion_code;
    uint64_t xhci_bot_csw_event_type;
    uint64_t xhci_bot_csw_event_trb0;
    uint64_t xhci_bot_csw_event_trb1;
    uint64_t xhci_bot_csw_event_trb2;
    uint64_t xhci_bot_csw_event_trb3;
    uint64_t xhci_bot_csw_signature;
    uint64_t xhci_bot_csw_tag;
    uint64_t xhci_bot_csw_residue;
    uint64_t xhci_bot_csw_status;
    uint64_t xhci_inquiry_peripheral;
    uint64_t xhci_inquiry_removable;
    uint64_t xhci_inquiry_version;
    uint64_t xhci_inquiry_response_format;
    uint64_t xhci_inquiry_additional_length;
    uint64_t xhci_inquiry_vendor_first8;
    uint64_t xhci_inquiry_product_first8;
    uint64_t xhci_inquiry_product_second8;
    uint64_t xhci_inquiry_revision;
    uint64_t xhci_bot_capacity_attempted;
    uint64_t xhci_bot_capacity_ok;
    uint64_t xhci_bot_capacity_tag;
    uint64_t xhci_bot_capacity_data_phys;
    uint64_t xhci_bot_capacity_cbw_completion_code;
    uint64_t xhci_bot_capacity_data_completion_code;
    uint64_t xhci_bot_capacity_csw_completion_code;
    uint64_t xhci_bot_capacity_csw_signature;
    uint64_t xhci_bot_capacity_csw_tag;
    uint64_t xhci_bot_capacity_csw_residue;
    uint64_t xhci_bot_capacity_csw_status;
    uint64_t xhci_bot_capacity_last_lba;
    uint64_t xhci_bot_capacity_block_size;
    uint64_t xhci_bot_capacity_block_count;
    uint64_t xhci_bot_read10_attempted;
    uint64_t xhci_bot_read10_ok;
    uint64_t xhci_bot_read10_tag;
    uint64_t xhci_bot_read10_lba;
    uint64_t xhci_bot_read10_bytes;
    uint64_t xhci_bot_read10_data_phys;
    uint64_t xhci_bot_read10_cbw_completion_code;
    uint64_t xhci_bot_read10_data_completion_code;
    uint64_t xhci_bot_read10_csw_completion_code;
    uint64_t xhci_bot_read10_csw_signature;
    uint64_t xhci_bot_read10_csw_tag;
    uint64_t xhci_bot_read10_csw_residue;
    uint64_t xhci_bot_read10_csw_status;
    uint64_t xhci_bot_read10_first8;
    uint64_t xhci_bot_read10_second8;
    uint64_t xhci_bot_write10_attempted;
    uint64_t xhci_bot_write10_ok;
    uint64_t xhci_bot_write10_verified;
    uint64_t xhci_bot_write10_tag;
    uint64_t xhci_bot_write10_lba;
    uint64_t xhci_bot_write10_bytes;
    uint64_t xhci_bot_write10_data_phys;
    uint64_t xhci_bot_write10_cbw_completion_code;
    uint64_t xhci_bot_write10_data_completion_code;
    uint64_t xhci_bot_write10_csw_completion_code;
    uint64_t xhci_bot_write10_csw_signature;
    uint64_t xhci_bot_write10_csw_tag;
    uint64_t xhci_bot_write10_csw_residue;
    uint64_t xhci_bot_write10_csw_status;
    uint64_t xhci_bot_write10_first8;
    uint64_t xhci_bot_write10_second8;
    char cpu_name[SYSINFO_CPU_NAME_MAX];
};

struct block_device_info {
    char name[BLOCK_DEVICE_NAME_MAX];
    uint64_t block_size;
    uint64_t block_count;
    uint64_t writable;
};

struct mount_info {
    char name[MOUNT_NAME_MAX];
    char path[MOUNT_PATH_MAX];
    char source[MOUNT_SOURCE_MAX];
    uint64_t writable;
};

uint64_t tpos_syscall2(uint64_t number, uint64_t arg0, uint64_t arg1);
uint64_t tpos_syscall3(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2);
uint64_t tpos_syscall4(uint64_t number, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3);

uint64_t tpos_write(uint64_t fd, const char *buffer, uint64_t length);
uint64_t tpos_getpid(void);
void tpos_yield(void);
void tpos_sleep_ticks(uint64_t ticks);
uint64_t tpos_brk(uint64_t new_break);
void *tpos_sbrk(uint64_t increment);
uint64_t tpos_open_flags(const char *path, uint64_t flags);
uint64_t tpos_open(const char *path);
uint64_t tpos_read(uint64_t fd, void *buffer, uint64_t length);
uint64_t tpos_close(uint64_t fd);
uint64_t tpos_getdents(const char *path, uint64_t index, struct dirent *dirent);
uint64_t tpos_dup2(uint64_t old_fd, uint64_t new_fd);
uint64_t tpos_lseek(uint64_t fd, int64_t offset, uint64_t whence);
uint64_t tpos_unlink(const char *path);
uint64_t tpos_spawn(const char *path, const char *args);
uint64_t tpos_task_info(uint64_t index, struct task_info *info);
uint64_t tpos_meminfo(struct meminfo *info);
uint64_t tpos_system_info(struct system_info *info);
uint64_t tpos_uptime(void);
uint64_t tpos_block_info(uint64_t index, struct block_device_info *info);
uint64_t tpos_block_read(uint64_t index, uint64_t lba, void *buffer, uint64_t count);
uint64_t tpos_block_write(uint64_t index, uint64_t lba, const void *buffer, uint64_t count);
uint64_t tpos_mount_info(uint64_t index, struct mount_info *info);
__attribute__((noreturn)) void tpos_exit(uint64_t status);

uint64_t tpos_strlen(const char *s);
uint64_t tpos_strcpy(char *dst, const char *src);
uint64_t tpos_u64_to_decimal(uint64_t value, char *buffer);
uint64_t tpos_u64_to_hex(uint64_t value, char *buffer);
void tpos_write_literal(const char *message, uint64_t length);
void tpos_write_cstr(const char *s);
void tpos_write_cstr_limited(const char *s, uint64_t max_len);
void tpos_write_u64_decimal(uint64_t value);
void tpos_write_u64_hex(uint64_t value);
void tpos_write_hex_byte(uint8_t value);

#endif
