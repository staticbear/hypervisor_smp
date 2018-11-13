#pragma once

#define QWORD	unsigned long long
#define DWORD	unsigned int
#define WORD 	unsigned short int
#define BYTE	unsigned char

typedef int bool;
#define TRUE  1
#define FALSE 0

enum
{	
	RAX_ = 0, 
    RCX_ = 1, 
    RDX_ = 2, 
    RBX_ = 3,
	RBP_ = 5,
	RSI_ = 6,
	RDI_ = 7
};

typedef struct{
	WORD 	PrevTaskLink;
	WORD 	Reserved1;
	DWORD 	ESP0;
	WORD 	SS0;
	WORD 	Reserved2;
	DWORD 	ESP1;
	WORD 	SS1;
	WORD 	Reserved3;
	DWORD 	ESP2;
	WORD 	SS2;
	WORD 	Reserved4;
	DWORD 	CR3;
	DWORD 	EIP;
	DWORD	EFLAGS;
	DWORD 	EAX;
	DWORD	ECX;
	DWORD	EDX;
	DWORD	EBX;
	DWORD	ESP;
	DWORD	EBP;
	DWORD	ESI;
	DWORD	EDI;
	WORD	ES;
	WORD	Reserved5;
	WORD	CS;
	WORD	Reserved6;
	WORD	SS;
	WORD	Reserved7;
	WORD	DS;
	WORD	Reserved8;
	WORD	FS;
	WORD	Reserved9;
	WORD	GS;
	WORD	Reserved10;
	WORD	LDTsel;
	WORD	Reserved11;
	
	WORD	T : 1;
	WORD	Reserved12 : 15;
	
	WORD	IOMap_addr;
} TSS_T;

typedef struct{
	QWORD ServiceNumber;
	QWORD Guest_Sys_CS;
	QWORD Guest_Sys_EIP;
	QWORD Guest_Sys_ESP;
} SysEnter_T;

