; MIT-0. Host test MS x64 ABI equivalent of SysV switch.
.code
tvox_context_swap PROC
 mov [rcx],rsp
 mov [rcx+8],rbx
 mov [rcx+16],rbp
 mov [rcx+24],r12
 mov [rcx+32],r13
 mov [rcx+40],r14
 mov [rcx+48],r15
 mov [rcx+56],rdi
 mov [rcx+64],rsi
 movdqu [rcx+80],xmm6
 movdqu [rcx+96],xmm7
 movdqu [rcx+112],xmm8
 movdqu [rcx+128],xmm9
 movdqu [rcx+144],xmm10
 movdqu [rcx+160],xmm11
 movdqu [rcx+176],xmm12
 movdqu [rcx+192],xmm13
 movdqu [rcx+208],xmm14
 movdqu [rcx+224],xmm15
 stmxcsr [rcx+240]
 fnstcw [rcx+244]
 mov rax,rdx
 mov rsp,[rax]
 mov rbx,[rax+8]
 mov rbp,[rax+16]
 mov r12,[rax+24]
 mov r13,[rax+32]
 mov r14,[rax+40]
 mov r15,[rax+48]
 mov rdi,[rax+56]
 mov rsi,[rax+64]
 movdqu xmm6,[rax+80]
 movdqu xmm7,[rax+96]
 movdqu xmm8,[rax+112]
 movdqu xmm9,[rax+128]
 movdqu xmm10,[rax+144]
 movdqu xmm11,[rax+160]
 movdqu xmm12,[rax+176]
 movdqu xmm13,[rax+192]
 movdqu xmm14,[rax+208]
 movdqu xmm15,[rax+224]
 ldmxcsr [rax+240]
 fldcw [rax+244]
 ret
tvox_context_swap ENDP
tvox_context_controls PROC
 stmxcsr [rcx+240]
 fnstcw [rcx+244]
 ret
tvox_context_controls ENDP
END
