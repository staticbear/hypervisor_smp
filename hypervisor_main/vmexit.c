#include "vmexit.h"
#include "defines.h"
#include "types.h"
#include "defines.h"
#include "memory_map.h"
#include "msr_regs.h"
#include "inline_asm.h"
#include "serialport_log64.h"
#include "long_mode.h"
#include "vmx.h"
#include "apic.h"

/*---------------------------------------------------------------------------------------------------*/

void ChangeProcessNames(DWORD SPI_GVA, DWORD SPI_size) // SPI  - SYSTEM_PROCESS_INFORMATION, GVA - Guest Virtual Address
{
	QWORD PhysAddr;
	DWORD *ptrPhysAddr;
	DWORD Offset = 0;
	WORD SizeOnPage;
	
	DWORD ProcNameGVA;
	void *ptrProcNamePhys;
	
	DWORD NextEntryOffset;
	
	if(SPI_size == 0)
		return;
	
	while(1)
	{
		PhysAddr = GuestLinAddrToPhysAddr(SPI_GVA + Offset + 60);
		SizeOnPage = 0x1000 - (WORD)(PhysAddr & 0xFFF);
		if(SizeOnPage < 4)
			return;
		
		ptrPhysAddr = (DWORD *)PhysAddr;
		ProcNameGVA = *(DWORD*)ptrPhysAddr;
		
		if(ProcNameGVA != 0)
		{
			PhysAddr = GuestLinAddrToPhysAddr(ProcNameGVA);
			ptrProcNamePhys = (void *)PhysAddr;
			SizeOnPage = 0x1000 - (WORD)(PhysAddr & 0xFFF);
			if(SizeOnPage > 6)
			{
				*(DWORD *)ptrProcNamePhys = 0x0029003A;		// :)
			}
		}
		
		PhysAddr = GuestLinAddrToPhysAddr(SPI_GVA + Offset);
		SizeOnPage = 0x1000 - (WORD)(PhysAddr & 0xFFF);
		if(SizeOnPage < 4)
			return;
		
		ptrPhysAddr = (DWORD *)PhysAddr;
		NextEntryOffset = *(DWORD *)ptrPhysAddr;
		if(NextEntryOffset == 0)
			return;
		
		Offset += NextEntryOffset;
		if(Offset > SPI_size)
			return;
	}
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static bool EXCPT_hndlr()
{
	/* VM-exit interruption information  00004404H */
	BYTE excpt_n = vmread(0x4404) & 0xFF;	

	/* GP */
	if(excpt_n == 13)
	{
		QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
		SysEnter_T *ptrSysEnter = (SysEnter_T *)(SYSENTER_addr + SYSENTER_size * GetCpuN());
		QWORD Guest_RIP = vmread(0x681E);	
		WORD *ptrInstr = (WORD*)GuestLinAddrToPhysAddr(Guest_RIP);
		
		if(*ptrInstr == 0x340F){																	// SYSENTER
			ptrSysEnter->ServiceNumber = (DWORD)ptrGUEST_REGS[RAX_];
			GuestSYSENTER(ptrSysEnter->Guest_Sys_CS, ptrSysEnter->Guest_Sys_EIP, ptrSysEnter->Guest_Sys_ESP);
		}
		else if(*ptrInstr == 0x350F){																// SYSEXIT
			
			if(ptrSysEnter->ServiceNumber == 0x105)													// NTQuerySystemInformation
			{
				QWORD PhysAddr = GuestLinAddrToPhysAddr((DWORD)ptrGUEST_REGS[RCX_]);
				WORD SizeOnPage = 0x1000 - (WORD)(PhysAddr & 0xFFF);
				if(SizeOnPage >= 24 && PhysAddr != 0) //3 arg + return addr
				{
					//InitSerialPort();
					//SerialPrintDigit64(0x77);
					
					DWORD *ptrGuestStack = (DWORD *)PhysAddr;
					if(ptrGuestStack[2] == 5)														// System Process Information
					{
						ChangeProcessNames(ptrGuestStack[3], ptrGuestStack[4]);
					}
				}
			}
			GuestSYSEXIT(ptrSysEnter->Guest_Sys_CS, (DWORD)ptrGUEST_REGS[RDX_], (DWORD)ptrGUEST_REGS[RCX_]);
		}
		else{
			DWORD err_code = vmread(0x4406);
			GuestGPInit(err_code);
		}
		return TRUE;
	}
	return FALSE;
}

/*---------------------------------------------------------------------------------------------------*/

static void INIT_hndlr()
{
	vmwrite(0x4826, WT_SIPI_ST);
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static void SIPI_hndlr()
{
	QWORD vm_rd_val = vmread(0x6400);							/* Exit qualification */
	
	/* Guest RIP 0000681EH */
	vmwrite(0x681E, 0);
	/* Guest CS selector  00000802H */
	vmwrite(0x802, (vm_rd_val << 8) & 0xFFFF);
	/* Guest CS base  00006808H */
	vmwrite(0x6808, (vm_rd_val << 12) & 0xFF000);
	/* Guest activity state 00004826H */
	vmwrite(0x4826, 0);											/* 0 - Active state	*/

	return;
}

/*---------------------------------------------------------------------------------------------------*/

static void TaskSwch_hndlr()
{
	DWORD exq_task = vmread(0x6400);							/* Exit qualification */
	
	int SwitchInit = (exq_task >> 30) & 3;
	
	int NewTSSsel = (exq_task & 0xFFFF);
	
	int OldTSSsel =  vmread(0x80E);								/* Guest TR selector  */
	
	GuestTaskSwitch(SwitchInit, OldTSSsel, NewTSSsel);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static void CPUID_hndlr()
{
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	
	DWORD eax_v, ecx_v, edx_v, ebx_v;
	DWORD cpuid_eax = (DWORD)ptrGUEST_REGS[RAX_];
	DWORD cpuid_ecx = (DWORD)ptrGUEST_REGS[RCX_];

	if(cpuid_eax == 0x80000002){
		eax_v = 0x20584D56;
		ebx_v = 0x64757453; 
		ecx_v = 0x6F432079;
		edx_v = 0x293A6572;
	}
	else if(cpuid_eax == 0x80000003 || cpuid_eax == 0x80000004){
		eax_v = 0;
		ebx_v = 0; 
		ecx_v = 0;
		edx_v = 0;
	}
	else
		cpuid(cpuid_eax, cpuid_ecx, &eax_v, &ecx_v, &edx_v, &ebx_v);
	
	ptrGUEST_REGS[RAX_] = eax_v;
	ptrGUEST_REGS[RCX_] = ecx_v;
	ptrGUEST_REGS[RDX_] = edx_v;
	ptrGUEST_REGS[RBX_] = ebx_v;

	// add cpuid length to Guest RIP 
	QWORD Guest_RIP	= vmread(0x681E);
	Guest_RIP += 2;
	vmwrite(0x681E, Guest_RIP);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static bool CR_Reg_hndlr()
{
	QWORD vm_rd_val = vmread(0x6400);							/* Exit qualification */
	
	BYTE  CRn = vm_rd_val & 0xF;
	
	BYTE  AC_type = vm_rd_val & 0x30;							/* Access type: must be 0 = MOV to CR */ 
	
	if(AC_type != 0 || CRn != 0){															
		SerialPrintStr64("Access to CR: error");
		return FALSE;
	}
	
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	BYTE gp_reg_n = (BYTE)((vm_rd_val >> 8) & 0xF);				/* [11:8] - Number of general-purpose register */
		
	QWORD gp_reg_val;
	if(gp_reg_n == 4)											/* RSP case, it's strange, but still possible */
		gp_reg_val = vmread(0x681C);							/* Guest RSP */
	else
		gp_reg_val = ptrGUEST_REGS[gp_reg_n];
	
	/* CR0 read shadow 00006004H */
	vmwrite(0x6004, gp_reg_val);
	/* Guest CR0 00006800H */
	vmwrite(0x6800, gp_reg_val & 0x9FFFFFFF);					/* clear CD and WT */

	/* add mov CRn length to Guest RIP 0000681EH */
	vm_rd_val = vmread(0x681E);									
	vm_rd_val += 3;
	vmwrite(0x681E, vm_rd_val);

	return TRUE;
}

/*---------------------------------------------------------------------------------------------------*/

static void XSETBV_hndlr()
{
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	
	xsetbv((DWORD)ptrGUEST_REGS[RAX_], (DWORD)ptrGUEST_REGS[RCX_], (DWORD)ptrGUEST_REGS[RDX_]);
	
	// add xsetbv length to Guest RIP 
	QWORD Guest_RIP	= vmread(0x681E);
	Guest_RIP += 3;
	vmwrite(0x681E, Guest_RIP);
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

static bool RDMSR_hndlr()
{
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	SysEnter_T *ptrSysEnter = (SysEnter_T *)(SYSENTER_addr + SYSENTER_size * GetCpuN());
	bool rt = FALSE;
	
	if((DWORD)ptrGUEST_REGS[RCX_] == 0x174)
	{
		ptrGUEST_REGS[RAX_] = ptrSysEnter->Guest_Sys_CS;
		ptrGUEST_REGS[RDX_] = 0;
		rt = TRUE;
	}
	
	if(rt == TRUE){
		// add rdmsr length to Guest RIP 
		QWORD Guest_RIP	= vmread(0x681E);
		Guest_RIP += 2;
		vmwrite(0x681E, Guest_RIP);
	}
	
	return rt;
}

/*---------------------------------------------------------------------------------------------------*/

static bool WRMSR_hndlr()
{
	QWORD *ptrGUEST_REGS = (QWORD *)(GUEST_REGS_addr + GUEST_REGS_size * GetCpuN());
	SysEnter_T *ptrSysEnter = (SysEnter_T *)(SYSENTER_addr + SYSENTER_size * GetCpuN());
	bool rt = FALSE;
	
	if((DWORD)ptrGUEST_REGS[RCX_] == 0x174)
	{
		ptrSysEnter->Guest_Sys_CS = (DWORD)ptrGUEST_REGS[RAX_];
		vmwrite(0x482A, 0);
		rt = TRUE;
	}
	else if((DWORD)ptrGUEST_REGS[RCX_] == 0x175)
	{
		ptrSysEnter->Guest_Sys_ESP = (DWORD)ptrGUEST_REGS[RAX_];
		vmwrite(0x6824, (DWORD)ptrGUEST_REGS[RAX_]);
		rt = TRUE;
	}
	else if((DWORD)ptrGUEST_REGS[RCX_] == 0x176)
	{
		ptrSysEnter->Guest_Sys_EIP = (DWORD)ptrGUEST_REGS[RAX_];
		vmwrite(0x6826, (DWORD)ptrGUEST_REGS[RAX_]);
		rt = TRUE;
	}
	
	if(rt == TRUE){
		// add wrmsr length to Guest RIP 
		QWORD Guest_RIP	= vmread(0x681E);
		Guest_RIP += 2;
		vmwrite(0x681E, Guest_RIP);
	}
	
	return rt;
}

/*---------------------------------------------------------------------------------------------------*/

void VMEXIT_handler()
{
	QWORD vm_rd_val;
	
	/* determine exit reason 00004402H */
	vm_rd_val = vmread(0x4402);
	
	switch(vm_rd_val & 0xFFFF)
	{
		/* Exception or non-maskable interrupt (NMI) */
		case 0:{
			if(!EXCPT_hndlr()){
				while(1){
					HLT();
				}
			}
			break;
		}
		/* INIT signal */
		case 3:{
			INIT_hndlr();
			break;
		}
		/* Start-up IPI (SIPI). */
		case 4:{
			SIPI_hndlr();
			break;
		}
		/* Task switch */
		case 9:{
			TaskSwch_hndlr();
			break;
		}
		/* CPUID. Guest software attempted to execute CPUID. */
		case 10:{
			CPUID_hndlr();
			break;
		}
		/* Control-register accesses. */
		case 28:{
			if(!CR_Reg_hndlr()){
				while(1){
					HLT();
				}
			}
			break;
		}
		/* RDMSR */
		case 31:{
			if(!RDMSR_hndlr()){
				while(1){
					HLT();
				}
			}
			break;
		}
		/* WRMSR */
		case 32:{
			if(!WRMSR_hndlr()){
				while(1){
					HLT();
				}
			}
			break;
		}
		/* XSETBV. Guest software attempted to execute XSETBV */
		case 55:{
			XSETBV_hndlr();
			break;
		}
		default:{
			//InitSerialPort();
			//SerialPrintStr64("VMEXIT: unexpected case");
			SerialPrintDigit64(vm_rd_val);
			while(1){
				HLT();
			}
		}
	}
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void VMEnter_error()
{
	QWORD vm_rd_val = vmread(0x4402);							/* Exit reason */
	InitSerialPort();
	SerialPrintDigit64(vm_rd_val);
	while(1){
		HLT();
	}
}

