#include "vmx.h"
#include "defines.h"
#include "types.h"
#include "defines.h"
#include "memory_map.h"
#include "msr_regs.h"
#include "inline_asm.h"
#include "serialport_log64.h"
#include "vmexit_wrapper.h"
#include "apic.h"

//#define BOCHS_DEBUG 1
/*---------------------------------------------------------------------------------------------------*/

bool CheckVMXConditions()
{
	BYTE rcx_val;
	asm volatile (
		"mov     eax, 1\r\n"
		"cpuid\r\n"  
		:"=c"(rcx_val)::"edx", "ebx"
	);
	
	if(!(rcx_val & (1UL << 5))){						/* CPUID.1:ECX.VMX[bit 5] must be 1 */
		SerialPrintStr64("CheckVMX: 1");
		return FALSE;
	}
	QWORD msr_rd_val = rdmsr(IA32_FEATURE_CONTROL);
	
	#ifdef BOCHS_DEBUG	
	wrmsr(IA32_FEATURE_CONTROL, msr_rd_val | 0x05);	
	#else
	if((BYTE)(msr_rd_val & 0x5) != 0x5){				/* Bit 0 is the lock bit,Bit 2 enables VMXON outside SMX operation*/
		SerialPrintStr64("CheckVMX: 2");
		return FALSE;
	}	
	#endif
	
	msr_rd_val = rdmsr(IA32_VMX_BASIC);
	if(!(msr_rd_val & (1UL << 55))){					/* Bit 55 is read as 1 if any VMX controls that default to 1 may be cleared to 0.*/
		SerialPrintStr64("CheckVMX: 3");
		return FALSE;
	}
		
	if(((msr_rd_val >> 32) & 0x1FFF) > 0x1000){			/* my realisation support only 4096 VMXON area*/
		SerialPrintStr64("CheckVMX: 4");
		return FALSE;									
	}
		
	if(((msr_rd_val >> 50) & 0xF) != CACHE_TP_WB){		/* Bits 53:50 report the memory type ( must be 6 - WB ) */
		SerialPrintStr64("CheckVMX: 5");
		return FALSE;
	}		

	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/	

bool InitVMX()
{
	DWORD  *ptrVMXON_addr = (DWORD *)(VMXON_addr + VMXON_size * GetCpuN());
	QWORD msr_rd_val = rdmsr(IA32_VMX_BASIC);
	
	// Initialize the version identifier in the VMXON region (the first 31 bits)
	ptrVMXON_addr[0] = (DWORD)(msr_rd_val & 0xFFFFFFFF);	

	// Ensure the current processor operating mode meets the required CR0 fixed bits
	msr_rd_val = rdmsr(IA32_VMX_CR0_FIXED0);
	QWORD cr0_fixed0 = msr_rd_val & 0xFFFFFFFF;	
	
	msr_rd_val = rdmsr(IA32_VMX_CR0_FIXED1);
	QWORD cr0_fixed1 = msr_rd_val & 0xFFFFFFFF;
	
	QWORD cr0_val = rdCR0();
	cr0_val = cr0_val & 
			  cr0_fixed1 | 
			  cr0_fixed0;
	wrCR0(cr0_val);
	
	//Ensure the resultant CR4 value supports all the CR4 fixed bits
	msr_rd_val = rdmsr(IA32_VMX_CR4_FIXED0);
	QWORD cr4_fixed0 = msr_rd_val & 0xFFFFFFFF;	
	
	msr_rd_val = rdmsr(IA32_VMX_CR4_FIXED1);
	QWORD cr4_fixed1 = msr_rd_val & 0xFFFFFFFF;
	
	QWORD cr4_val = rdCR4();
	cr4_val =  cr4_val & 
			   cr4_fixed1 | 
			   cr4_fixed0 |
			   (1UL << 13);								// Enable VMX operation by setting CR4.VMXE = 1
	wrCR4(cr4_val);
	
	// Execute VMXON with the physical address of the VMXON region as the operand.
	if(!vmxon((void*)(VMXON_addr + VMXON_size * GetCpuN()))){
		SerialPrintStr64("VMXON error");
		return FALSE;
	}
	
	void *ptrVMCS_addr = (void*)(VMCS_addr + VMXON_size * GetCpuN());
	
	// Clear VMCS area
	for(int i = 0; i < 4096; i+=8)
		*(QWORD *)(ptrVMCS_addr + i) = 0;
	
	// Initialize the version identifier in the VMCS (first 31 bits)
	msr_rd_val = rdmsr(IA32_VMX_BASIC);
	*(DWORD*)(ptrVMCS_addr) = (DWORD)(msr_rd_val & 0xFFFFFFFF);
	
	// Execute the VMCLEAR
	if(!vmclear(ptrVMCS_addr)){
		SerialPrintStr64("VMCLEAR error");
		return FALSE;
	}
	
	// Execute the VMPTRLD
	if(!vmptrld(ptrVMCS_addr)){
		SerialPrintStr64("VMPTRLD error");
		return FALSE;
	}
	
	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/	

void InitGuestRegisterState()
{
	QWORD msr_rd_val;
				
	// Guest CR0 00006800H
	QWORD cr0_val = (1UL << 4);							// ET Extension Type 
					
	msr_rd_val = rdmsr(IA32_VMX_CR0_FIXED0);
	QWORD cr0_fixed0 = msr_rd_val & 0x1FFFFFFE;			// clear PG, PE, PE, CD, NW
	
	msr_rd_val = rdmsr(IA32_VMX_CR0_FIXED1);
	QWORD cr0_fixed1 = msr_rd_val & 0xFFFFFFFF;
	
	cr0_val = cr0_val & 
			  cr0_fixed1 | 
			  cr0_fixed0;
			  
	vmwrite(0x6800, cr0_val);
	
	// Guest CR3 00006802H
	vmwrite(0x6802, 0);
	
	// Guest CR4 00006804H		
	QWORD cr4_val = 0;
	msr_rd_val = rdmsr(IA32_VMX_CR4_FIXED0);
	QWORD cr4_fixed0 = msr_rd_val & 0xFFFFFFFF;		

	msr_rd_val = rdmsr(IA32_VMX_CR4_FIXED1);
	QWORD cr4_fixed1 = msr_rd_val & 0xFFFFFFFF;
			
	cr4_val = cr4_val & 
			  cr4_fixed1 | 
			  cr4_fixed0;
	vmwrite(0x6804, cr4_val);
	
	//Guest DR7 0000681AH
	vmwrite(0x681A, 0);
	
	// Guest RSP 0000681CH
	vmwrite(0x681C, 0xFFD6);
	
	// Guest RIP 0000681EH
	if(IsBsp())
		vmwrite(0x681E, 0x7C00);
	else
		vmwrite(0x681E, 0x3F);

	// Guest RFLAGS 00006820H
	vmwrite(0x6820, 0x82);

	// The following fields for each of the registers CS, SS, DS, ES, FS, GS, LDTR, and TR:
	// Guest ES base  00006806H
	vmwrite(0x6806, 0x0);

	// Guest CS base  00006808H
	vmwrite(0x6808, 0x0);

	// Guest SS base  0000680AH
	vmwrite(0x680A, 0x0);

	// Guest DS base  0000680CH
	vmwrite(0x680C, 0x0);

	// Guest FS base  0000680EH
	vmwrite(0x680E, 0x0);

	// Guest GS base  00006810H
	vmwrite(0x6810, 0x0);
	
	// Guest LDTR base  00006812H
	vmwrite(0x6812, 0x0);

	// Guest TR base  00006814H
	vmwrite(0x6814, 0x0);

	// Guest ES limit  00004800H
	vmwrite(0x4800, 0xFFFFFFFF);  					// limit for unreal mode

	// Guest CS limit  00004802H
	vmwrite(0x4802, 0xFFFF);

	// Guest SS limit  00004804H
	vmwrite(0x4804, 0xFFFF);

	// Guest DS limit  00004806H
	vmwrite(0x4806, 0xFFFFFFFF);					// limit for unreal mode

	// Guest FS limit  00004808H
	vmwrite(0x4808, 0xFFFF);

	// Guest GS limit  0000480AH
	vmwrite(0x480A, 0xFFFF);

	// Guest LDTR limit  0000480CH
	vmwrite(0x480C, 0xFFFF);

	// Guest TR limit  0000480EH
	vmwrite(0x480E, 0xFFFF);

	// Guest ES access rights  00004814H
	vmwrite(0x4814, 0xF093);

	// Guest CS access rights  00004816H
	vmwrite(0x4816, 0x93);

	// Guest SS access rights  00004818H
	vmwrite(0x4818, 0x93);

	// Guest DS access rights  0000481AH
	vmwrite(0x481A, 0xF093);

	// Guest FS access rights  0000481CH
	vmwrite(0x481C, 0x93);

	// Guest GS access rights  0000481EH
	vmwrite(0x481E, 0x93);

	// Guest LDTR access rights  00004820H
	vmwrite(0x4820, 0x82);

	// Guest TR access rights  00004822H
	vmwrite(0x4822, 0x8B);

	// Guest ES selector  00000800H
	vmwrite(0x800, 0x0);

	// Guest CS selector  00000802H
	if(IsBsp())
		vmwrite(0x802, 0x0);
	else
		vmwrite(0x802, 0x2000);

	// Guest SS selector  00000804H
	vmwrite(0x804, 0x0);

	// Guest DS selector  00000806H
	vmwrite(0x806, 0x0);

	// Guest FS selector  00000808H
	vmwrite(0x808, 0x0);

	// Guest GS selector  0000080AH
	vmwrite(0x80A, 0x0);

	// Guest LDTR selector  0000080CH
	vmwrite(0x80C, 0x0);

	// Guest TR selector  0000080EH
	vmwrite(0x80E, 0x0);

	// Guest GDTR base 00006816H
	vmwrite(0x6816, 0x0);

	// Guest IDTR base 00006818H
	vmwrite(0x6818, 0x0);

	// Guest GDTR limit 00004810H
	vmwrite(0x4810, 0x0);

	// Guest IDTR limit 00004812H
	vmwrite(0x4812, 0x3FF);

	// Guest IA32_PAT 00002804H
	msr_rd_val = rdmsr(IA32_PAT);
	vmwrite(0x2804, msr_rd_val);

	return;
}

/*---------------------------------------------------------------------------------------------------*/	

void InitGuestNonRegisterState()
{
	QWORD guest_state;
	if(IsBsp())
		guest_state = ACTIVE_ST;									
	else
		guest_state = HLT_ST;									
	
	// Guest activity state 00004826H
	vmwrite(0x4826, guest_state);													
	
	// Guest interruptibility state 00004824H
	vmwrite(0x4824, 0x0);

	// VMCS link pointer 00002800H
	vmwrite(0x2800, 0xFFFFFFFFFFFFFFFF);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/	

void InitHostStateArea()
{
	QWORD msr_rd_val;
	
	// Host CR0 00006C00H
	vmwrite(0x6C00, 0x80000039);

	// Host CR3 00006C02H
	vmwrite(0x6C02, PML4_addr);

	// Host CR4 00006C04H
	#ifdef BOCHS_DEBUG	
	vmwrite(0x6C04, 0x20A1);
	#else
	vmwrite(0x6C04, 0x420A1);
	#endif
	
	// Host RSP 00006C14H
	QWORD stack = STACK64_addr - STACK64_size * GetCpuN();
	vmwrite(0x6C14, stack);

	// Host RIP 00006C16H
	vmwrite(0x6C16, get_vmexit_addr());
	
	// Host ES selector 00000C00H
	vmwrite(0xC00, 0x10);

	// Host CS selector 00000C02H
	vmwrite(0xC02, 0x08);

	// Host SS selector 00000C04H
	vmwrite(0xC04, 0x10);

	// Host DS selector 00000C06H
	vmwrite(0xC06, 0x10);

	// Host FS selector 00000C08H
	vmwrite(0xC08, 0x10);

	// Host GS selector 00000C0AH
	vmwrite(0xC0A, 0x10);

	// Host TR selector 00000C0CH
	QWORD TSS_seg = 3 * 8 + 16 * GetCpuN();
	vmwrite(0xC0C, TSS_seg);

	// Host TR base 00006C0AH
	QWORD TSS_addr_n = TSS_addr + TSS_size * GetCpuN();
	vmwrite(0x6C0A, TSS_addr_n);

	// Host GDTR base 00006C0CH
	vmwrite(0x6C0C, GDT64_addr);

	// Host IDTR base 00006C0EH
	vmwrite(0x6C0E, IDT64_addr);

	// Host IA32_PAT 00002804H
	msr_rd_val = rdmsr(IA32_PAT);
	vmwrite(0x2804, msr_rd_val);
	
	// Host IA32_EFER 2C02H
	msr_rd_val = rdmsr(IA32_EFER);
	vmwrite(0x2C02, msr_rd_val);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/	

bool InitExecutionControlFields()
{
	QWORD msr_rd_val;
	
	// Pin-based VM-execution controls 00004000H
	msr_rd_val = rdmsr(IA32_VMX_TRUE_PINBASED_CTLS);										
	vmwrite(0x4000, msr_rd_val );											// Activate VMX preemption timer debug
		
	// Primary processor-based VM-execution controls 00004002H
	msr_rd_val = rdmsr(IA32_VMX_TRUE_PROCBASED_CTLS);
	QWORD prim_proc_val = (1UL << 31) |										// Activate secondary controls
						  (1UL << 28);										// Use MSR bitmaps
						  
	if((prim_proc_val & (msr_rd_val >> 32)) != prim_proc_val){
		SerialPrintStr64("primary processor-based unsupported value");
		return FALSE;
	}
	vmwrite(0x4002, prim_proc_val | (msr_rd_val & 0xFFFFFFFF));

	// Secondary processor-based VM-execution controls2 0000401EH
	msr_rd_val = rdmsr(IA32_VMX_PROCBASED_CTLS2);
	QWORD scnd_proc_val = (1UL << 7) |										// Unrestricted guest
						  (1UL << 5) |										// Enable VPID
						  (1UL << 3) |										// Enable RDTSCP	
						  (1UL << 1);										// Enable EPT
						  
	if((scnd_proc_val & (msr_rd_val >> 32)) != scnd_proc_val){
		SerialPrintStr64("secondary processor-based unsupported value");
		return FALSE;
	}					  
	vmwrite(0x401E, scnd_proc_val | (msr_rd_val & 0xFFFFFFFF));
	
	// Exception bitmap 00004004H
	DWORD 	except_bitmap = (1UL << 13);									// General Protection																	
	vmwrite(0x4004, except_bitmap);
	
	//clear I/O bitmaps  A and B
	QWORD *ptrIO_BMPs = (QWORD *)IO_BMPA_addr;
	for(int i = 0; i < (IO_BMPA_size + IO_BMPB_size) / 8; i++)
		ptrIO_BMPs[i] = 0;

	// I/O bitmap A 00002000H  0000h - 7FFFh
	vmwrite(0x2000, IO_BMPA_addr);

	// I/O bitmap B 00002002H  8000h - FFFFh
	vmwrite(0x2002, IO_BMPB_addr);

	// CR0 guest/host mask 00006000H
	QWORD cr0_mask = (1UL << 30) |											// CD
					 (1UL << 29);											// NW
	vmwrite(0x6000, cr0_mask);

	// CR4 guest/host mask 00006002H
	vmwrite(0x6002, 0);

	// CR0 read shadow 00006004H
	QWORD cr0_rd_shadow = (1UL << 30) |										// CD
						  (1UL << 29);										// NW
	vmwrite(0x6004, cr0_rd_shadow);

	// CR4 read shadow 00006006H
	vmwrite(0x6006, 0);

	// clear MSR bitmap
	QWORD *ptrMSR_BMP = (QWORD *)MSR_BMP_addr;
	for(int i = 0; i < MSR_BMP_size / 8; i++)
		ptrMSR_BMP[i] = 0;
	
	// 174H 372 IA32_SYSENTER_CS SYSENTER_CS read mask
	ptrMSR_BMP[0x174 >> 6] |= (1UL  << (0x174 & 0x3F));
	
	// 174H 372 IA32_SYSENTER_CS SYSENTER_CS write mask
	ptrMSR_BMP[0x100 + (0x174 >> 6)] |= (1UL << (0x174 & 0x3F));
	// 175H 373 IA32_SYSENTER_ESP SYSENTER_ESP write mask
	ptrMSR_BMP[0x100 + (0x175 >> 6)] |= (1UL  << (0x175 & 0x3F));
	// 176H 374 IA32_SYSENTER_EIP SYSENTER_EIP write mask
	ptrMSR_BMP[0x100 + (0x176 >> 6)] |= (1UL  << (0x176 & 0x3F));
	
	// Address of MSR bitmap  00002004H
	vmwrite(0x2004, MSR_BMP_addr);

	// EPT pointer 0000201AH
	QWORD EPTPval = EPT_PML4_addr |
					(3 << 3)	  |											// page walk 4 - 1
					CACHE_TP_WB;
	vmwrite(0x201A, EPTPval);
  
	// Virtual-processor identifier (VPID) 00000000H
	vmwrite(0x0, VPID + GetCpuN());

	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/	

static void ChangeEPTMemType(QWORD vaddr, QWORD size, BYTE type)
{
	QWORD *ptrEPT_PT = (QWORD *)(EPT_PT_addr + ((vaddr & 0xFFFFF000) >> 9));				// vaddr / 4096 * 8
	type = (type & 0x7) << 3;
	
	for(QWORD i = 0; i < (size >> 12); i++)
		ptrEPT_PT[i] = (ptrEPT_PT[i] & 0xFFFFFFFFFFFFFFC7) | type;

	return;
}

/*---------------------------------------------------------------------------------------------------*/

void InitEPT()
{
	// clear EPT structures
	QWORD *ptrEPT_PML4 = (QWORD *)EPT_PML4_addr;
	for(int i = 0; i < EPT_STRUCT_SIZEx8; i++)
		ptrEPT_PML4[i] = 0;

    // EPT PML4
	QWORD EPT_PML4_entry = EPT_PDPT_addr |
							(1UL << 2) |								// Execute access
							(1UL << 1) |								// Write access
							(1UL << 0);									// Read access 
							
	for(int i = 0; i < EPT_PML4E_CNT; i++)
		ptrEPT_PML4[i] = EPT_PML4_entry;

    // EPT Page Directory Pointer Table
	QWORD *ptrEPT_PDPT = (QWORD *)EPT_PDPT_addr;
	QWORD EPT_PDPT_entry = EPT_PD_addr |
							(1UL << 2) |								// Execute access
							(1UL << 1) |								// Write access
							(1UL << 0);									// Read access 
							
	for(int i = 0; i < EPT_PDPTE_CNT; i++){
		ptrEPT_PDPT[i] = EPT_PDPT_entry;	
		EPT_PDPT_entry += TABLE_SZ;
	}		

    // EPT Page Directoryes
	QWORD *ptrEPT_PD = (QWORD *)EPT_PD_addr;
	QWORD EPT_PD_entry = EPT_PT_addr |
							(1UL << 2) |								// Execute access
							(1UL << 1) |								// Write access
							(1UL << 0);									// Read access 
							
	for(int i = 0; i < EPT_PDE_CNT; i++){
		ptrEPT_PD[i] = EPT_PD_entry;	
		EPT_PD_entry += TABLE_SZ;
	}
							
    // EPT Page tables 
	QWORD *ptrEPT_PT = (QWORD *)EPT_PT_addr;
	QWORD EPT_PT_entry = (1UL << 6) |									// Ignore PAT memory type for this 4-KByte page
						 (CACHE_TP_WB << 3)	|							// memory type 
						 (1UL << 2) |									// Execute access
						 (1UL << 1) |									// Write access
						 (1UL << 0);									// Read access 
						 
	for(int i = 0; i < EPT_RAM_MAX / PAGE_SZ; i++){
		ptrEPT_PT[i] = EPT_PT_entry;	
		EPT_PT_entry += TABLE_SZ;
	}
	
    // change EPT Memory Type where it need
	
	ChangeEPTMemType(DEV_RAM_addr, DEV_RAM_size, CACHE_TP_UC);
	
	ChangeEPTMemType(VIDEO_RAM0_addr, VIDEO_RAM0_size, CACHE_TP_UC);

	ChangeEPTMemType(VIDEO_RAM1_addr, VIDEO_RAM1_size, CACHE_TP_WC);

	ChangeEPTMemType(VIDEO_RAM2_addr, VIDEO_RAM2_size, CACHE_TP_WC);

	ChangeEPTMemType(VIDEO_RAM3_addr, VIDEO_RAM3_size, CACHE_TP_WC);

	return;
}

/*---------------------------------------------------------------------------------------------------*/

bool InitVMExitControl()
{
	QWORD msr_rd_val;
	
	// VM-exit controls 000000110B 0000400CH
	msr_rd_val = rdmsr(IA32_VMX_TRUE_EXIT_CTLS);
	QWORD vmext_ctrl_val = /*(1UL << 22) |*/							// Save VMX preemption timer value debug
						   (1UL << 21) |								// Load IA32_EFER
						   (1UL << 20) |								// Save IA32_EFER
						   (1UL << 19) |								// Load IA32_PAT
						   (1UL << 18) |								// Save IA32_PAT
						   (1UL << 9);									// Host address space size

	if((vmext_ctrl_val & (msr_rd_val >> 32)) != vmext_ctrl_val){
		SerialPrintStr64("vm-exit fields error");
		return FALSE;
	}
	vmwrite(0x400C, vmext_ctrl_val | (msr_rd_val & 0xFFFFFFFF));
	
	// VM-exit MSR-store count 0000400EH
	vmwrite(0x400E, 3);

	// VM-exit MSR-store address 00002006H
	QWORD *ptrMSR_EX_STR = (QWORD *)(MSR_EX_STR_addr + MSR_EX_STR_size * GetCpuN());
	vmwrite(0x2006, (QWORD)ptrMSR_EX_STR);

	// VM-exit MSR-load count 00004010H
	vmwrite(0x4010, 3);

	// VM-exit MSR-load address 00002008H
	QWORD *ptrMSR_EX_LDR = (QWORD *)(MSR_EX_LDR_addr + MSR_EX_LDR_size * GetCpuN());
	
	ptrMSR_EX_LDR[0] = IA32_MTRR_PHYSBASE0;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EX_LDR[1] = rdmsr(IA32_MTRR_PHYSBASE0);						// 127:64 MSR data
      
	ptrMSR_EX_LDR[2] = IA32_MTRR_PHYSMASK0;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EX_LDR[3] = rdmsr(IA32_MTRR_PHYSMASK0);						// 127:64 MSR data

	ptrMSR_EX_LDR[4] = IA32_MTRR_DEF_TYPE;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EX_LDR[5] = rdmsr(IA32_MTRR_DEF_TYPE);						// 127:64 MSR data
	  
	vmwrite(0x2008, (QWORD)ptrMSR_EX_LDR);	
		
	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/

bool InitVMEntryControl()
{
	QWORD msr_rd_val;
	
	// VM-entry controls 00004012H
	msr_rd_val = rdmsr(IA32_VMX_TRUE_ENTRY_CTLS);
	QWORD vment_ctrl_val = (1UL << 15) |								// Load IA32_EFER
						   (1UL << 14);									// Load IA32_PAT

	if((vment_ctrl_val & (msr_rd_val >> 32)) != vment_ctrl_val){
		SerialPrintStr64("vm-entry fields error");
		return FALSE;
	}
	vmwrite(0x4012, vment_ctrl_val | (msr_rd_val & 0xFFFFFFFF));

	// VM-entry MSR-load count 00004014H
	vmwrite(0x4014, 3);

	// VM-entry MSR-load address 0000200AH
	QWORD *ptrMSR_EN_LDR = (QWORD *)(MSR_EN_LDR_addr + MSR_EN_LDR_size * GetCpuN());
	
	ptrMSR_EN_LDR[0] = IA32_MTRR_PHYSBASE0;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EN_LDR[1] = rdmsr(IA32_MTRR_PHYSBASE0);						// 127:64 MSR data
      
	ptrMSR_EN_LDR[2] = IA32_MTRR_PHYSMASK0;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EN_LDR[3] = rdmsr(IA32_MTRR_PHYSMASK0);						// 127:64 MSR data

	ptrMSR_EN_LDR[4] = IA32_MTRR_DEF_TYPE;								// 63:32 Reserved, 31:0 MSR index
	ptrMSR_EN_LDR[5] = rdmsr(IA32_MTRR_DEF_TYPE);						// 127:64 MSR data
	
	vmwrite(0x200A, (QWORD)ptrMSR_EN_LDR);
	
	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/

QWORD GuestLinAddrToPhysAddr(DWORD GuestLinAddr)
{
	DWORD *ptrGuestPD = (DWORD *)(vmread(0x6802) & 0xFFFFF000);					// Guest CR3 , Page Directory
	
	QWORD GuestPTi = ptrGuestPD[(GuestLinAddr >> 22) & 0x3FF]; 	
	
	if(GuestPTi & (1UL << 7))							
		return (GuestPTi & 0xFFC00000) + (GuestLinAddr & 0x3FFFFF);				// Page size 4Mb
	else
		GuestPTi &= 0xFFFFF000;													// Page size 4Kb
		
	DWORD *ptrGuestPT = (DWORD *)GuestPTi;
	
	QWORD ptrGuestPage = ptrGuestPT[(GuestLinAddr >> 12) & 0x3FF];				// Page
	
	return (ptrGuestPage & 0xFFFFF000) + (GuestLinAddr & 0xFFF);
}

/*---------------------------------------------------------------------------------------------------*/

void GetSegmentInfo(int dscr_sel, QWORD *base, DWORD *limit, DWORD *access_right)
{
	int dscr_idx = (dscr_sel >> 3);
	
	QWORD *ptrGDT = (QWORD *)GuestLinAddrToPhysAddr(vmread(0x6816));			// Guest GDTR base
	
	*base = (QWORD)(((ptrGDT[dscr_idx] >> 16) & 0xFFFFFF) |
					((ptrGDT[dscr_idx] >> 32) & 0xFF000000)
				    );
					
	*limit = (DWORD)((ptrGDT[dscr_idx] & 0xFFFF) |
					 ((ptrGDT[dscr_idx] >> 32) & 0xF0000)
				    );
					
	if(ptrGDT[dscr_idx] & (1UL << 55))											// if bit G is set in descriptor 
		*limit = (*limit * 4096) + 4096 - 1;
					
	*access_right = (DWORD)((ptrGDT[dscr_idx] >> 40) & 0xFFFF);	

	if(!(ptrGDT[dscr_idx] & (1UL << 47)))
		*access_right |= (1UL << 16);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestTaskSwitch(int SwitchInit, int OldTSSsel, int NewTSSsel)
{
	
	int OldTSS_idx = (OldTSSsel >> 3);
	int NewTSS_idx = (NewTSSsel >> 3);
	
	QWORD *ptrGDT = (QWORD *)GuestLinAddrToPhysAddr(vmread(0x6816));			// Guest GDTR base
	
	DWORD OldTSS = (((ptrGDT[OldTSS_idx] >> 16) & 0xFFFFFF) |
					((ptrGDT[OldTSS_idx] >> 32) & 0xFF000000)
				   );
				   
	TSS_T *ptrOldTSS = (TSS_T *)GuestLinAddrToPhysAddr(OldTSS);
					
	DWORD NewTSS = (((ptrGDT[NewTSS_idx] >> 16) & 0xFFFFFF) |
					((ptrGDT[NewTSS_idx] >> 32) & 0xFF000000)
				   );
				   
	TSS_T *ptrNewTSS = (TSS_T *)GuestLinAddrToPhysAddr(NewTSS);
			
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	
	// Save current guest state into old TSS
	ptrOldTSS->CR3 		= (DWORD)vmread(0x6802);
	
	ptrOldTSS->EIP 		= (DWORD)vmread(0x681E);
	
	ptrOldTSS->EFLAGS 	= (DWORD)vmread(0x6820);
	
	ptrOldTSS->EAX		= (DWORD)ptrGUEST_REGS[RAX_];
	
	ptrOldTSS->ECX		= (DWORD)ptrGUEST_REGS[RCX_];
	
	ptrOldTSS->EDX		= (DWORD)ptrGUEST_REGS[RDX_];
	
	ptrOldTSS->EBX		= (DWORD)ptrGUEST_REGS[RBX_];
	
	ptrOldTSS->ESP 		= (DWORD)vmread(0x681C);
	
	ptrOldTSS->EBP		= (DWORD)ptrGUEST_REGS[RBP_];
	
	ptrOldTSS->ESI		= (DWORD)ptrGUEST_REGS[RSI_];
	
	ptrOldTSS->EDI		= (DWORD)ptrGUEST_REGS[RDI_];
	
	ptrOldTSS->ES		= (WORD)vmread(0x800);
	
	ptrOldTSS->CS		= (WORD)vmread(0x802);
	
	ptrOldTSS->SS		= (WORD)vmread(0x804);
	
	ptrOldTSS->DS		= (WORD)vmread(0x806);
	
	ptrOldTSS->FS		= (WORD)vmread(0x808);
	
	ptrOldTSS->GS		= (WORD)vmread(0x80A);
	
	ptrOldTSS->LDTsel	= (WORD)vmread(0x80C);
	
	// 6. If the task switch was initiated with a JMP or IRET instruction, the processor clears the busy (B) flag
	if(SwitchInit == 1 || 						//  IRET
	   SwitchInit == 2){						//  JMP
		ptrGDT[OldTSS_idx] &= ~(1UL << 41);
	}
	
	// 7. If the task switch was initiated with an IRET instruction, the processor clears the NT flag 
	if(SwitchInit == 1)
		ptrOldTSS->EFLAGS &= ~(1UL << 14);
	
	// Load guest state from new TSS
	vmwrite(0x6802, ptrNewTSS->CR3);			// Guest CR3
	
	vmwrite(0x681E, ptrNewTSS->EIP);			// Guest EIP
	
	ptrNewTSS->EFLAGS |= (1UL << 1);			// Guest EFLAGS set 1 bit (reserved)
	
	ptrGUEST_REGS[RAX_] = ptrNewTSS->EAX;		// Guest EAX
	
	ptrGUEST_REGS[RCX_] = ptrNewTSS->ECX;		// Guest ECX
	
	ptrGUEST_REGS[RDX_] = ptrNewTSS->EDX;		// Guest EDX
	
	ptrGUEST_REGS[RBX_] = ptrNewTSS->EBX;		// Guest EBX
	
	vmwrite(0x681C, ptrNewTSS->ESP);			// Guest ESP
	
	ptrGUEST_REGS[RBP_] = ptrNewTSS->EBP;		// Guest EBP
	
	ptrGUEST_REGS[RSI_] = ptrNewTSS->ESI;		// Guest ESI
	
	ptrGUEST_REGS[RDI_] = ptrNewTSS->EDI;		// Guest EDI
	
	// 9. If the task switch was initiated with a CALL instruction, an exception, or an interrupt, 
	//    the processor will set the NT flag
	if(SwitchInit == 0 || 						// CALL
	   SwitchInit == 3){						// Task Gate in IDT
		vmwrite(0x6820, ptrNewTSS->EFLAGS | (1UL << 14));		
	}
	else{
		vmwrite(0x6820, ptrNewTSS->EFLAGS);		
	}
	   
	// 10. If the task switch was initiated with a CALL instruction, JMP instruction, an exception, 
	//	  or an interrupt, the processor sets the busy (B) flag
	if(SwitchInit != 1){						// IRET
		ptrGDT[NewTSS_idx] |= (1UL << 41);
	}
	
	// 11. Loads the task register with the segment selector and descriptor for the new task's TSS
	QWORD seg_base; 
	DWORD seg_limit, seg_access_right;
	
	GetSegmentInfo(NewTSSsel, &seg_base, &seg_limit, &seg_access_right);							// TSS
	
	// Guest TR selector 0000080EH
	vmwrite(0x80E, NewTSSsel);
	
	// Guest TR limit 0000480EH
	vmwrite(0x480E, seg_limit);
	
	// Guest TR access rights  00004822
	vmwrite(0x4822, seg_access_right);
	
	// Guest TR base 00006814
	vmwrite(0x6814, seg_base);
	
	GetSegmentInfo(ptrNewTSS->ES, &seg_base, &seg_limit, &seg_access_right);						// ES
	
	// Guest ES selector 00000800H
	vmwrite(0x800, ptrNewTSS->ES);
	
	// Guest ES limit 00004800H
	vmwrite(0x4800, seg_limit);
	
	// Guest ES access rights  00004814H
	vmwrite(0x4814, seg_access_right);
	
	// Guest ES base 00006806H
	vmwrite(0x6806, seg_base);
	
	GetSegmentInfo(ptrNewTSS->CS, &seg_base, &seg_limit, &seg_access_right);						// CS
	
	// Guest CS selector 00000802H
	vmwrite(0x802, ptrNewTSS->CS);
	
	// Guest CS limit 00004802H
	vmwrite(0x4802, seg_limit);
	
	// Guest CS access rights  00004816H
	vmwrite(0x4816, seg_access_right);
	
	// Guest CS base 00006808H
	vmwrite(0x6808, seg_base);
	
	GetSegmentInfo(ptrNewTSS->SS, &seg_base, &seg_limit, &seg_access_right);						// SS
	
	// Guest SS selector 00000804H
	vmwrite(0x804, ptrNewTSS->SS);
	
	// Guest SS limit 00004804H
	vmwrite(0x4804, seg_limit);
	
	// Guest SS access rights  00004818H
	vmwrite(0x4818, seg_access_right);
	
	// Guest SS base 0000680AH
	vmwrite(0x680A, seg_base);
	
	GetSegmentInfo(ptrNewTSS->DS, &seg_base, &seg_limit, &seg_access_right);						// DS
	
	// Guest DS selector 00000806H
	vmwrite(0x806, ptrNewTSS->DS);
	
	// Guest DS limit 00004806H
	vmwrite(0x4806, seg_limit);
	
	// Guest DS access rights  0000481AH
	vmwrite(0x481A, seg_access_right);
	
	// Guest DS base 0000680CH
	vmwrite(0x680C, seg_base);
	
	GetSegmentInfo(ptrNewTSS->FS, &seg_base, &seg_limit, &seg_access_right);						// FS
	
	// Guest FS selector 00000808H
	vmwrite(0x808, ptrNewTSS->FS);
	
	// Guest FS limit 00004808H
	vmwrite(0x4808, seg_limit);
	
	// Guest FS access rights  0000481CH 
	vmwrite(0x481C, seg_access_right);
	
	// Guest FS base 0000680EH
	vmwrite(0x680E, seg_base);
	
	GetSegmentInfo(ptrNewTSS->GS, &seg_base, &seg_limit, &seg_access_right);						// GS
	
	// Guest GS selector 0000080AH
	vmwrite(0x80A, ptrNewTSS->GS);
	
	// Guest GS limit 0000480AH
	vmwrite(0x480A, seg_limit);
	
	// Guest GS access rights  0000481EH
	vmwrite(0x481E, seg_access_right);
	
	// Guest GS base 00006810H
	vmwrite(0x6810, seg_base);
	
	GetSegmentInfo(ptrNewTSS->LDTsel, &seg_base, &seg_limit, &seg_access_right);					// LDT
	
	// Guest LDTR selector 0000080CH
	vmwrite(0x80C, ptrNewTSS->LDTsel);
	
	// Guest LDTR limit 0000480CH
	vmwrite(0x480C, seg_limit);
	
	// Guest LDTR access rights 00004820H
	vmwrite(0x4820, seg_access_right);
	
	// Guest LDTR base 00006812H
	vmwrite(0x6812, seg_base);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestSYSENTER(DWORD TargetCS, DWORD TargetEIP, DWORD TargetESP)
{
	// IF CR0.PE = 0 OR IA32_SYSENTER_CS[15:2] = 0 THEN #GP(0); FI;
	QWORD GuestCR0 = vmread(0x6800);
	
	if((GuestCR0 & 0x1) == 0 || (TargetCS & 0xFFFC) == 0)
		GuestGPInit(0);
	
	QWORD GuestRFLAGS = vmread(0x6820);
	GuestRFLAGS &= ~((1UL << 9) | 				// RFLAGS.IF ← 0; (* Mask interrupts *)
					 (1UL << 17));				// RFLAGS.VM ← 0; (* Ensures protected mode execution *)
	vmwrite(0x6820, GuestRFLAGS);
	
	// ESP ← IA32_SYSENTER_ESP[31:0];
	vmwrite(0x681C, TargetESP);
	
	// EIP ← IA32_SYSENTER_EIP[31:0];
	vmwrite(0x681E, TargetEIP);
	
	// CS.Selector ← IA32_SYSENTER_CS[15:0] AND FFFCH;
	vmwrite(0x802, TargetCS & 0xFFFC);
	
	// CS.Base ← 0; (* Flat segment *)
	vmwrite(0x6808, 0x0);
	
	// CS.Limit ← FFFFFH; (* With 4-KByte granularity, implies a 4-GByte limit *)
	vmwrite(0x4802, 0xFFFFFFFF);
	
	DWORD GuestCS_AR = (0x0B << 0)| 			// CS.Type ← 11;
					   (1UL << 4) | 			// CS.S ← 1;
					   (1UL << 7) | 			// CS.P ← 1;
					   (1UL << 14)| 			// CS.D ← 1; (* 32-bit code segment*)
					   (1UL << 15); 			// CS.G ← 1; (* 4-KByte granularity *)
	vmwrite(0x4816, GuestCS_AR);
	
	// SS.Selector ← CS.Selector + 8; (* SS just above CS *)
	vmwrite(0x804, (TargetCS & 0xFFFC) + 8);
	
	// SS.Base ← 0; (* Flat segment *)
	vmwrite(0x680A, 0x0);
	
	// SS.Limit ← FFFFFH; (* With 4-KByte granularity, implies a 4-GByte limit *)
	vmwrite(0x4804, 0xFFFFFFFF);
	
	/*
	SS.DPL ← 0;
	*/
	DWORD GuestSS_AR = (0x03 << 0)| 			// SS.Type ← 3; (* Read/write data, accessed *)
					   (1UL << 4) | 			// SS.S ← 1;
					   (1UL << 7) | 			// SS.P ← 1;
					   (1UL << 14)| 			// SS.B ← 1; (* 32-bit stack segment*)
					   (1UL << 15); 			// SS.G ← 1; (* 4-KByte granularity *)
	vmwrite(0x4818, GuestSS_AR);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestSYSEXIT(DWORD TargetCS, DWORD TargetEIP, DWORD TargetESP)
{
	// IF CR0.PE = 0 OR CPL ≠ 0 THEN #GP(0); FI;
	QWORD GuestCR0 = vmread(0x6800);
	
	if((GuestCR0 & 0x1) == 0)			// for full compatibility need check condition GuestCPL == 0
		GuestGPInit(0);
	
	// ESP ← TargetESP (ECX);
	vmwrite(0x681C, TargetESP);
	
	// EIP ← TargetEIP (EDX);
	vmwrite(0x681E, TargetEIP);
	
	// CS.Selector ← IA32_SYSENTER_CS[15:0] + 16;
	vmwrite(0x802, ((TargetCS & 0xFFFC) + 16) | 3);
	
	// CS.Base ← 0; (* Flat segment *)
	vmwrite(0x6808, 0x0);
	
	// CS.Limit ← FFFFFH; (* With 4-KByte granularity, implies a 4-GByte limit *)
	vmwrite(0x4802, 0xFFFFFFFF);
	
	DWORD GuestCS_AR = (0x0B << 0)| 			// CS.Type ← 11;
					   (1UL << 4) | 			// CS.S ← 1;
					   (3UL << 5) |				// CS.DPL ← 3;
					   (1UL << 7) | 			// CS.P ← 1;
					   (1UL << 14)| 			// CS.D ← 1; (* 32-bit code segment*)
					   (1UL << 15); 			// CS.G ← 1; (* 4-KByte granularity *)
	vmwrite(0x4816, GuestCS_AR);
	
	// SS.Selector ← CS.Selector + 8; (* SS just above CS *)
	vmwrite(0x804, ((TargetCS & 0xFFFC) + 16 + 8) | 3);
	
	// SS.Base ← 0; (* Flat segment *)
	vmwrite(0x680A, 0x0);
	
	// SS.Limit ← FFFFFH; (* With 4-KByte granularity, implies a 4-GByte limit *)
	vmwrite(0x4804, 0xFFFFFFFF);  // guest limit field measured in bytes!11
	
	DWORD GuestSS_AR = (0x03 << 0)| 			// SS.Type ← 3; (* Read/write data, accessed *)
					   (1UL << 4) | 			// SS.S ← 1;
					   (3UL << 5) |				// SS.DPL ← 3;
					   (1UL << 7) | 			// SS.P ← 1;
					   (1UL << 14)| 			// SS.B ← 1; (* 32-bit stack segment*)
					   (1UL << 15); 			// SS.G ← 1; (* 4-KByte granularity *)
	vmwrite(0x4818, GuestSS_AR);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestPageFaultInit(DWORD Guest_VA)
{
	/* VM-entry interruption-information field 00004016H */
	DWORD int_inf_field = (1UL << 31) |							/* Valid bit */	
						  (1UL << 11) |							/* Deliver error code (0 = do not deliver; 1 = deliver) */	
						  (3UL << 8)  |							/* 3 - Hardware exception */
						   14;									/* 14 - Page Fault */
									   
	vmwrite(0x4016, int_inf_field);
				
	/* VM-entry exception error code 00004018H */
	DWORD excpt_err_code = (1UL << 2);							/* A user-mode access caused the fault. */
				
	vmwrite(0x4018, excpt_err_code);
				
	wrCR2(Guest_VA);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestUDInit()
{
	/* VM-entry interruption-information field 00004016H */
	DWORD int_inf_field = (1UL << 31) |							/* Valid bit */	
						  (3UL << 8)  |							/* 3 - Hardware exception */
						   6;									/* 6 - Invalid Opcode Exception */
									   
	vmwrite(0x4016, int_inf_field);
				
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void GuestGPInit(DWORD err_code)
{
	/* VM-entry interruption-information field 00004016H */
	DWORD int_inf_field = (1UL << 31) |							/* Valid bit */	
						  (3UL << 8)  |							/* 3 - Hardware exception */
						   13;									/* 13 - General protection */
		
		
	QWORD Guest_CR0 = vmread(0x6800);
	
	if(Guest_CR0 & 0x01){
		int_inf_field |= (1UL << 11);							/* error code present */
		vmwrite(0x4018, err_code);
	}
	
	vmwrite(0x4016, int_inf_field);

	return;
}