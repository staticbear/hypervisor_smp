#include "serialport_log64.h"
#include "types.h"
#include "inline_asm.h"
#include "memory_map.h"


void InitSerialPort()
{

	outb(COM1 + 1, 0); 		/* Disable all interrupts */
	
	outb(COM1 + 3, 0x80); 	/* Enable DLAB (set baud rate divisor) */

	outb(COM1 + 0, 14); 	/* Set divisor to 14 (lo byte) (8000 baud equ 9600 baud in my PC, I don't know why) */

	outb(COM1 + 1, 0); 		/* (hi byte) */
	
	outb(COM1 + 3, 0x03); 	/* 8 bits, no parity, one stop bit */

	outb(COM1 + 2, 0xC7); 	/* Enable FIFO, clear them, with 14-byte threshold */

	outb(COM1 + 4, 0x0B); 	/* IRQs enabled, RTS/DSR set */

	return;
}


/*---------------------------------------------------------------------------------------------------*/

void SerialPrintStr64(BYTE *ptrStr)
{
	int i = 0;
	if(ptrStr[i] == 0)
		return;
	
	EnterSpinLock((void *)(SERIAL_EVENT_addr));
	
	do
	{
		while(!(inb(COM1 + 5) & 0x20));
		outb(COM1, ptrStr[i]);
		i++;
		
	}while(ptrStr[i]);
	
	while(!(inb(COM1 + 5) & 0x20));
	outb(COM1, 0x0D);

	ExitSpinLock((void *)(SERIAL_EVENT_addr));
	
	return;
}

/*---------------------------------------------------------------------------------------------------*/

void SerialPrintDigit64(QWORD val)
{
	EnterSpinLock((void *)(SERIAL_EVENT_addr));
	
	for(int i = 15; i >= 0; i--){
		
		BYTE smbl = (val >> i * 4) & 0xF;
		
		if(smbl >= 0xA)
			smbl += 0x57;
		else
			smbl += 0x30;
		
		while(!(inb(COM1 + 5) & 0x20));
		outb(COM1, smbl);
	}
	
	while(!(inb(COM1 + 5) & 0x20));
	outb(COM1, 0x0D);
	
	ExitSpinLock((void *)(SERIAL_EVENT_addr));

	return;
}

