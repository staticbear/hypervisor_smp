#include "hypervisor.h"
#include "types.h"
#include "defines.h"
#include "memory_map.h"
#include "long_mode.h"
#include "serialport_log64.h"
#include "isr_wrapper.h"
#include "inline_asm.h"
#include "vmx.h"
#include "vmexit.h"
#include "apic.h"

void main()
{
	if(IsBsp()){
		ExitSpinLock((void *)(SERIAL_EVENT_addr));
		InitLongModeGdt();	
		InitLongModeIdt(get_isr_addr());
		// disable USB Legacy support
		outd(0x430, ind(0x430) & ~(1UL << 17));		
	}
		
	lgdt((void*)GDT64_addr, (3 * 8 + 16 * N_CORES) - 1);							  
	lidt((void*)IDT64_addr, 0x1FF);
	
	InitLongModeTSS();
					
	if(IsBsp())
		InitLongModePages();							
	
	InitPAT();
	InitMTRR();
	
	InitControlAndSegmenRegs();
	
	if(!CheckVMXConditions())
		while(1);
	
	InitVMX();
	
	InitGuestRegisterState();
	
	InitGuestNonRegisterState();
	
	InitHostStateArea();
	
	if(IsBsp()){
		InitEPT();
	}
	
	if(!InitExecutionControlFields())
		while(1);
	
	if(!InitVMExitControl())
		while(1);
	
	if(!InitVMEntryControl())
		while(1);
	
	if(IsBsp()){
		for(int i = 1; i < N_CORES; i++)
			WakeupAP(i, 0x20);
	}
	
	if(!IsBsp()){
		SerialPrintStr64("AP started.");
	}
	else{
		// enable USB Legacy support
		outd(0x430, ind(0x430) | (1UL << 17));
		// enable PIC
		outb(0xA1, 0);
		outb(0x21, 0);
	}
	
	VMLaunch();
	
	VMEnter_error();	
}
