#ifndef ACPI_H
#define ACPI_H

/* ACPI power management: poweroff and reboot */
void acpi_poweroff(void);
void acpi_reboot(void);

/* Initialize ACPI subsystem (RSDP discovery, FADT parsing, SMI enable) */
void acpi_init(void);

/* Returns 1 if ACPI was successfully initialized */
int acpi_is_enabled(void);

#endif
