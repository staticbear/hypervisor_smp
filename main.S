#include "memory_map.h"

.intel_syntax noprefix
.globl  init
.align 	4
init:
	mov		eax, 1
	cpuid
	shr		ebx, 24
	mov		rax, STACK64_size
	mul		rbx
	mov		rdx, STACK64_addr
	sub		rdx, rax
	mov		rsp, rdx
	jmp		main
	