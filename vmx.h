#pragma once
#include "types.h"
bool CheckVMXConditions(void);
bool InitVMX(void);
void InitGuestRegisterState(void);
void InitGuestNonRegisterState(void);
void InitHostStateArea(void);
bool InitExecutionControlFields(void);
void InitEPT(void);
bool InitVMExitControl(void);
bool InitVMEntryControl(void);
QWORD GuestLinAddrToPhysAddr(DWORD GuestLinAddr);
void GetSegmentInfo(int dscr_sel, QWORD *base, DWORD *limit, DWORD *access_right);
void GuestTaskSwitch(int SwitchInit, int OldTSSsel, int NewTSSsel);
void GuestSYSENTER(DWORD TargetCS, DWORD TargetEIP, DWORD TargetESP);
void GuestSYSEXIT(DWORD TargetCS, DWORD TargetEIP, DWORD TargetESP);
void GuestPageFaultInit(DWORD Guest_VA);
void GuestUDInit(void);
void GuestGPInit(DWORD err_code);

enum
{	
	ACTIVE_ST = 0, 
    HLT_ST, 
    SHUTDOWN_ST, 
    WT_SIPI_ST
};