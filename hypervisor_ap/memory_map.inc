
SMP_V_addr              equ     0x20000
SMP_V_size              equ     0x2000

IDT64_size              equ     0x1000
IDT64_addr              equ     (SMP_V_addr + SMP_V_size)

INIT32_PML4_size        equ     (1 * 512 *8)
INIT32_PML4_addr        equ     (IDT64_addr + IDT64_size)

INIT32_PDPT_size        equ     (1 * 512 * 8)
INIT32_PDPT_addr        equ     (INIT32_PML4_addr + INIT32_PML4_size)

VAR_addr                equ     (INIT32_PDPT_addr + INIT32_PDPT_size)
VAR_size                equ     0x1000

STACK_size              equ     0x1000
STACK_addr              equ     (VAR_addr + VAR_size + STACK_size)
