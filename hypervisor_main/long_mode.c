#include "defines.h"
#include "types.h"
#include "defines.h"
#include "memory_map.h"
#include "msr_regs.h"
#include "inline_asm.h"
#include "long_mode.h"
#include "apic.h"


/*---------------------------------------------------------------------------------------------------*/

void InitLongModeGdt()
{
	QWORD *ptrGDT = (QWORD *)GDT64_addr;
	int n = 0;
	
	// NULL
	ptrGDT[n++] = 0;
	
	// CODE SEGMENT
	ptrGDT[n++] = (1UL << 53) | 						// L
				  (1UL << 47) |							// P
				  (1UL << 44) | (1UL << 43); 			// must be 0b11
				
	// DATA SEGMENT							 
	ptrGDT[n++] = (1UL << 47) |							// P
				  (1UL << 44) |							// must be 1
				  (1UL << 41);							// W
	
	// TSS64
	for(int i = 0; i < N_CORES; i++)
	{	
		QWORD TSS_addr_n = TSS_addr + TSS_size * GetCpuN();
		
		QWORD TSS_low = ((TSS_addr_n & 0xFF000000) << 56) | 	// Base Address[31:24]
						((TSS_addr_n & 0xFFFFFF) << 16)   |		// Base Address[23:0]
						(0x67) | 								// Segment Limit[15:0]
						(1UL << 47) |				 			// P
						(9UL << 40);				 			// Type TSS64
							
		QWORD TSS_high = (TSS_addr_n >> 32);
		
		ptrGDT[n++] = TSS_low;
		ptrGDT[n++] = TSS_high;
	}
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void InitLongModeIdt(QWORD VectorAddr)
{
	QWORD *ptrIDT = (QWORD *)IDT64_addr;
	QWORD IV_low =	(VectorAddr & 0xFFFF) | 
					((VectorAddr & 0xFFFF0000) << 32) |
					(1UL << 47) |				 		// P
					(14UL << 40)|				 		// Type interrupt gate
					(01UL << 32)|						// IST = 1
					(8UL << 16);						// code segment = 1 * 8
					
	QWORD IV_high = (VectorAddr >> 32);
	
	// make first 32 interrupts
	for(int i = 0; i < 32; i++){
		*(QWORD*)(ptrIDT++) = IV_low;
		*(QWORD*)(ptrIDT++) = IV_high;
	}
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void InitLongModeTSS()
{	
	DWORD *ptrTSSArea = (DWORD *)(TSS_addr + TSS_size * GetCpuN());
	for(int i = 0; i < 26; i++)
		ptrTSSArea[i] = 0;

	QWORD *ptrTSS = (QWORD *)(TSS_addr + TSS_size * GetCpuN() + 4);
	
	QWORD stack = STACK64_addr - STACK64_size * GetCpuN();
	
	ptrTSS[0] = stack;						// RSP0
	ptrTSS[1] = stack;						// RSP1
	ptrTSS[2] = stack;						// RSP2
	
	stack = STACK64i_addr - STACK64i_size * GetCpuN();
	
	for(int i = 4; i < 11; i++)
		ptrTSS[i] = stack;					// IST 1 - 7
	
	DWORD TSS_seg = 3 * 8 + 16 * GetCpuN();
	asm volatile( 
				  "mov     eax, %0\r\n"				// TSS SEGMENT
				  "ltr     ax" 
				  ::"r"(TSS_seg));
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void InitPAT()
{
	QWORD PAT = rdmsr(IA32_PAT);
	BYTE PA0 = PAT & 0x7;
	if(PA0 != CACHE_TP_WB){
		PAT = (PAT & ~0x7) | CACHE_TP_WB;
		wrmsr(IA32_PAT, PAT);
	}
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static inline QWORD get_max_phys_addr_mask()
{
	QWORD r_val;
    asm volatile ( 	"mov     eax, 0x80000008\r\n"
					"cpuid\r\n"                                   
					"xor     rdx, rdx\r\n"
					"dec     rdx\r\n"
					"mov     cl, 64\r\n"
					"sub     cl, al\r\n"
					"shr     rdx, cl\r\n"
					"mov	 %0, rdx"
					: "=r"(r_val)::"ebx");
	return r_val;
}

/*---------------------------------------------------------------------------------------------------*/

void InitMTRR()
{
	// set PHYS_BASE0
	QWORD Base = (HYPERVISOR_base & ~0xFFF) | CACHE_TP_WB;
	wrmsr(IA32_MTRR_PHYSBASE0, Base);
	
	// set PHYS_MASK0
	QWORD max_phys_addr = get_max_phys_addr_mask();
	
	QWORD Mask = (max_phys_addr - (HYPERVISOR_RAM_MAX - HYPERVISOR_base - 1)) | 	// mask = max_phys_addr - (area_end - area_begin - 1)
				 (1UL << 11);														// Valid bit
	
	wrmsr(IA32_MTRR_PHYSMASK0, Mask);
	
	// enable MTRR
	QWORD EnVal = (1UL << 10) |														// FE — Fixed-range MTRRs enable
				  (1UL << 11);														// E — MTRR enable
	wrmsr(IA32_MTRR_DEF_TYPE, EnVal);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void InitLongModePages()
{
	// PML4
	QWORD *ptrPML4 = (QWORD *)PML4_addr;
	//PLL4 entry = 512GB area
	QWORD PML4_enry = PDPT_addr  |
					  (1UL << 2) |						// U/S
					  (1UL << 1) |						// R/W
					  (1UL << 0);						// Present; must be 1 
			
	for(int i = 0; i < PML4_CNT * 512; i++){
		if(i < PML4E_CNT){
			ptrPML4[i] = PML4_enry;
			PML4_enry += TABLE_SZ;
		}
		else
			ptrPML4[i] = 0;
	}
	
	// Page Directory Pointer Table
	QWORD *ptrPDPT = (QWORD *)PDPT_addr;
	//PDPT entry = 1GB area
	QWORD PDPT_enry = PD_addr    |
					  (1UL << 2) |						// U/S
					  (1UL << 1) |						// R/W
					  (1UL << 0);						// Present; must be 1 
					  	  
	for(int i = 0; i < PDPT_CNT * 512; i++){
		if( i < PDPTE_CNT){
			ptrPDPT[i] = PDPT_enry;
			PDPT_enry += TABLE_SZ;
		}			
		else
			ptrPDPT[i] = 0;
	}
	
	// Page Directory
	QWORD *ptrPD = (QWORD *)PD_addr;
	//PD entry = 2MB area
	QWORD PhysAddr = 0;
	QWORD PD_enry;
					
	for(int i = 0; i < PD_CNT * 512; i++){
		if( i < PDE_CNT)
		{
			if(PhysAddr >= HYPERVISOR_base){
				PD_enry = PhysAddr |  
						  (1UL << 8) |						// Global Page	
						  (1UL << 7) |						// Page Size, must be 1 for 2MB pages
						  (1UL << 2) |						// U/S
						  (1UL << 1) |						// R/W
						  (1UL << 0);						// Present; must be 1 
			}
			else
			{
				PD_enry = PhysAddr |  
						  (1UL << 7) |						// Page Size, must be 1 for 2MB pages
						  (1UL << 4) |						// PCD
						  (1UL << 2) |						// U/S
						  (1UL << 1) |						// R/W
						  (1UL << 0);						// Present; must be 1 
			}
			ptrPD[i] = PD_enry;
			PhysAddr += 0x200000;	
		}
		else
			ptrPD[i] = 0;						
	}
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

inline void InitControlAndSegmenRegs()
{
	asm volatile ( "mov    	rax, CR0\r\n"
				   "and    	eax, 0x9FFFFFFF\r\n"            // clear CD(30), NE(29)
				   "mov    	CR0, rax\r\n"
				   "mov    	rax, %0\r\n"
				   "mov    	CR3, rax\r\n"
				   "mov    	ax, 2 * 8\r\n"					// DATA SEGMENT
				   "mov    	ds, ax\r\n"
				   "mov    	es, ax\r\n"
				   "mov    	ss, ax\r\n"
				   "call	get_offset%=\r\n"				// next part of code intent for reload segment register CS  
				   "get_offset%=:\r\n"
				   "pop		rax\r\n"
				   "add		rax, 0xE\r\n"
				   "push	1 * 8\r\n"						// CODE SEGMENT
				   "push	rax\r\n"
				   "mov		rax, rsp\r\n"
				   ".byte 0x4C, 0xFF, 0x28\r\n"				// I really did't find mnemonic for JMP m16:64 with REX
				   "add		rsp, 16"
				   ::"i"(PML4_addr)
				 );
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void Delay(int ms_cnt)
{
	ms_cnt *= 33941;
	ms_cnt >>= 9;
	
	BYTE rf_tgl = inb(0x61) & (1UL << 4);
	
	while(ms_cnt)
	{
		BYTE rf_tgl_tmp;
		while(rf_tgl == (rf_tgl_tmp = inb(0x61) & (1UL << 4)));
		rf_tgl = rf_tgl_tmp;
		ms_cnt--;
	}

	return;
}

/*---------------------------------------------------------------------------------------------------*/

void Speeker_Emit()
{
	outb(0x43, 0xB6);
	outb(0x42, 0x40);

	BYTE t = inb(0x61);
	t |= 3;
	
	outb(0x61, t);

	Delay(200);
	
	t = inb(0x61);
	t &= 0xFC;
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

/*
void LPC_Disable(bool en)
{
	if(en){
		outd(CONFIG_ADDRESS, 0x8000F882);
		outd(CONFIG_DATA, 0x3C0F0010);
	}
	else{
		outd(CONFIG_ADDRESS, 0x8000F880);
		outd(CONFIG_DATA, 0x3C0F0010);
	}
	return;
}
*/