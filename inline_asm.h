#pragma once
#include "types.h"

void EnterSpinLock(void *addr);
void ExitSpinLock(void *addr);
void cpuid(DWORD code_eax, DWORD code_ecx, DWORD* a, DWORD* b, DWORD* c, DWORD* d);
void xsetbv(DWORD a, DWORD c, DWORD d);
void lidt(void* base, WORD size);
void lgdt(void* base, WORD size);
QWORD rdmsr(DWORD msr_id);
void wrmsr(DWORD msr_id, QWORD msr_value);
void outb(WORD port, BYTE val);
void outd(WORD port, DWORD val);
BYTE inb(WORD port);
DWORD ind(WORD port);
void wrCR0(QWORD val);
QWORD rdCR0(void);
void wrCR2(QWORD val);
QWORD rdCR2(void);
void wrCR4(QWORD val);
QWORD rdCR4(void);
void wrDR0(QWORD val);
QWORD rdDR0(void);
void wrDR6(QWORD val);
QWORD rdDR6(void);
void HLT(void);
bool vmxon(void* addr);
bool vmclear(void* addr);
bool vmptrld(void* addr);
bool vmwrite(QWORD index, QWORD value);
QWORD vmread(QWORD index);
void VMLaunch(void);
void INVEPT(QWORD Type, QWORD EPTP);