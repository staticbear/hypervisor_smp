all:
	gcc -O -c -m64 -masm=intel main.S hypervisor.c long_mode.c serialport_log64.c inline_asm.c isr_wrapper.S interrupts.c vmx.c vmexit.c vmexit_wrapper.S apic.c 
	ld -o hypervisor.hex -T main.ld main.o hypervisor.o long_mode.o serialport_log64.o inline_asm.o isr_wrapper.o interrupts.o vmx.o vmexit.o vmexit_wrapper.o apic.o 
	rm main.o hypervisor.o long_mode.o serialport_log64.o inline_asm.o isr_wrapper.o interrupts.o vmx.o vmexit.o vmexit_wrapper.o apic.o
	