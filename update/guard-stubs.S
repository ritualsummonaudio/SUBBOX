.intel_syntax noprefix
.text
.macro STUB name,callee
.globl \name
.p2align 4
.seh_proc \name
\name:
    sub rsp,0xb8
    .seh_stackalloc 0xb8
    .seh_endprologue
    mov [rsp+0x20],rax
    mov [rsp+0x28],rcx
    mov [rsp+0x30],rdx
    mov [rsp+0x38],r8
    mov [rsp+0x40],r9
    mov [rsp+0x48],r10
    mov [rsp+0x50],r11
    movdqu [rsp+0x60],xmm0
    movdqu [rsp+0x70],xmm1
    movdqu [rsp+0x80],xmm3
    movdqu [rsp+0x90],xmm4
    movdqu [rsp+0xa0],xmm5
    mov rcx,rbx
    movaps xmm1,xmm0
    call \callee
    movq xmm2,xmm0
    movdqu xmm0,[rsp+0x60]
    movdqu xmm1,[rsp+0x70]
    movdqu xmm3,[rsp+0x80]
    movdqu xmm4,[rsp+0x90]
    movdqu xmm5,[rsp+0xa0]
    mov rax,[rsp+0x20]
    mov rcx,[rsp+0x28]
    mov rdx,[rsp+0x30]
    mov r8,[rsp+0x38]
    mov r9,[rsp+0x40]
    mov r10,[rsp+0x48]
    mov r11,[rsp+0x50]
    add rsp,0xb8
    ret
.seh_endproc
.endm
STUB sbo_stub_a,sbo_target_a
STUB sbo_stub_b,sbo_target_b
