

include 'memory_map.inc'
include 'defines.inc'
include 'msr_regs.inc'

use16
org	0
real_mode_begin:
	cli
	mov	ax, cs
	mov	ds, ax
	mov	ss, ax
	mov	es, ax
	xor	ax, ax
	mov	sp, ax

	; disable PIC
	mov	al, 0FFh
	out	0A1h, al
	out	21h, al

	 ; imcr access
	mov	al, 0x70
	out	0x22, al
	mov	al, 0x01		    ; set bit 1 for SMP mode
	out	0x23, al

	lgdt	fword ptr GDTR

	xor	ax, ax
	mov	es, ax
	mov	ax, 0xE0FF		    ; jmp eax
	mov	word ptr es:0x7000, ax

	mov	eax, CR0
	or	al, 1
	mov	CR0, eax		    ; set bit 0 PE

	mov	eax, main
	jmp	0x8:0x7000

.hlt_loop:
	hlt
	jmp    .hlt_loop

rb real_mode_begin + 256 - $

;----------------------------------------------------------
GDT:
   NULL_descr		db	8 dup (0)

   CS_SEGMENT		db	0FFh, 0FFh	; segment limit 15:0
			db	00h, 00h	; segment base addr 15:0
			db	00h		; base 23:16
			db	10011011b	;
			db	11001111b	;
			db	00h		; base 31:24

   DATA_SEGMENT 	db	0FFh, 0FFh	; segment limit 15:0
			db	00h, 00h	; segment base addr 15:0
			db	00h		; base 23:16
			db	10010010b
			db	11001111b
			db	00h		; base 31:24

   CS64_SEGMENT 	db	00h, 00h	; segment limit 15:0
			db	00h, 00h	; segment base addr 15:0
			db	00h		; base 23:16
			db	98h
			db	20h
			db	00h		; base 31:24

   DS64_SEGMENT 	db	00h, 00h	; segment limit 15:0
			db	00h, 00h	; segment base addr 15:0
			db	00h		; base 23:16
			db	92h
			db	20h
			db	00h		; base 31:24

GDT_size		equ $-GDT
GDTR			dw  GDT_size-1
			dd  GDT + 0x20000

use32
real_mode_end:

org	SMP_V_addr + (real_mode_end - real_mode_begin)

main:
	mov	ax, 10h
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	fs, ax
	mov	gs, ax
	mov	esp, STACK_addr

	; make PML4
	mov    edi, INIT32_PML4_addr
	mov    eax, (INIT32_PDPT_addr or 7)
	stosd
	xor    eax, eax
	stosd

	; make Page Directory Pointer Table
	mov    edi, INIT32_PDPT_addr
	mov    eax, 0x87			; 1 GB Page
	xor    edx, edx
	mov    ecx, PDPTE_CNT
.nxt_pdpte:
	stosd
	xchg   eax, edx
	stosd
	xchg   eax, edx
	add    eax, 0x40000000
	jnc    @f
	inc    edx
@@:
	dec    ecx
	jnz    .nxt_pdpte

	mov    eax, CR4
	or     eax, 0xA0			; set PAE(5), PGE(7)
	mov    CR4, eax

	mov    eax, INIT32_PML4_addr
	mov    CR3, eax

	mov    ecx, IA32_EFER
	rdmsr
	or     eax, 0x00000100			; set LME (8)
	wrmsr

	mov    eax, CR0
	or     eax, 0xE0000000			; set PG(31), CD(30), NE(29)
	mov    CR0, eax

	jmp    3*8:long_mode

use64
long_mode:

	mov	ax, 4 * 8
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	fs, ax
	mov	gs, ax

	; make IDT
	mov	rdi, IDT64_addr
	mov	rdx, common_int
	mov	rax, 0x8E0100080000		; present and type = 1110 interrupt gate
	mov	ax, dx				; offset 15..0
	and	edx, 0xFFFF0000
	shl	rdx, 32
	or	rax, rdx			; offset 31..16
	mov	rdx, common_int
	shr	rdx, 32
	mov	rcx, 32
.nxt_idte:
	stosq
	xchg	rax, rdx
	stosq
	xchg	rax, rdx
	dec	rcx
	jnz	.nxt_idte

	xor	rax, rax
	push	rax
	mov	rax, IDT64_addr
	shl	rax, 16
	mov	ax, 0x1FF
	push	rax
	lidt	[rsp]
	add	rsp, 16

	mov	rax, 0x100000000
	jmp	rax

;----------------------------------------------------------

common_int:
	mov	rax, 0xDEADC0DE
	push	rax
	call	SerialPrintDigit64
	jmp	$

include 'serialport_log64.inc'





