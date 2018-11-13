#include "types.h"
#include "apic.h"
#include "long_mode.h"
#include "inline_asm.h"
#include "msr_regs.h"
#include "serialport_log64.h"
/*---------------------------------------------------------------------------------------------------*/

void WakeupAP(BYTE ap_n, BYTE vector)
{
	DWORD *ptrICR_L = (DWORD *)0xFEE00300;
	DWORD *ptrICR_H = (DWORD *)0xFEE00310;
	
	// INIT
	*ptrICR_H = (ap_n << 24);
	
	*ptrICR_L = (5UL << 8)  |  				// 101 - INIT 
				(1UL << 14);				// Level assert 
				
	Delay(10);
	
	// SIPI
	*ptrICR_H = (ap_n << 24);
	
	*ptrICR_L = (6UL << 8)  |  				// 110 - SIPI
				(1UL << 14) |				// Level assert 
				vector;
		
	Delay(200);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void IntInit4TLBShutdown()
{
	DWORD *ptrICR_L = (DWORD *)0xFEE00300;
	DWORD *ptrICR_H = (DWORD *)0xFEE00310;
	
	// INIT
	*ptrICR_H = 0;
	
	*ptrICR_L = (5UL << 8)  |  				// 101 - INIT 
				(1UL << 14) |				// Level assert 
				(3UL << 18);				// 11: All Excluding Self
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

bool IsBsp()
{
	if(rdmsr(IA32_APIC_BASE) & (1UL << 8))
		return TRUE;
	else
		return FALSE;
}

/*---------------------------------------------------------------------------------------------------*/

BYTE GetCpuN()
{
	const volatile DWORD *cpu_n = (DWORD*)0xFEE00020;
	return (BYTE)(*cpu_n >> 24) & 0xFF;
}
