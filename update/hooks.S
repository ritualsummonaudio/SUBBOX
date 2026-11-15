.intel_syntax noprefix
.text
.globl frame_stub
.p2align 4
.seh_proc frame_stub
frame_stub:
    sub rsp,0xc8
    .seh_stackalloc 0xc8
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
    movdqu [rsp+0x80],xmm2
    movdqu [rsp+0x90],xmm3
    movdqu [rsp+0xa0],xmm4
    movdqu [rsp+0xb0],xmm5
    mov rcx,rdi
    lea rdx,[rbp+0x140]
    mov r8,[rsp+0x138]
    call frame_update
    movdqu xmm0,[rsp+0x60]
    movdqu xmm1,[rsp+0x70]
    movdqu xmm2,[rsp+0x80]
    movdqu xmm3,[rsp+0x90]
    movdqu xmm4,[rsp+0xa0]
    movdqu xmm5,[rsp+0xb0]
    mov rax,[rsp+0x20]
    mov rcx,[rsp+0x28]
    mov rdx,[rsp+0x30]
    mov r8,[rsp+0x38]
    mov r9,[rsp+0x40]
    mov r10,[rsp+0x48]
    mov r11,[rsp+0x50]
    add rsp,0xc8
    ret
.seh_endproc

.globl original_path
.p2align 4
original_path:
    .byte 0xe9
    .long 0
.globl original_pow
.p2align 4
original_pow:
    .byte 0xe9
    .long 0
.globl original_cos
.p2align 4
original_cos:
    .byte 0xe9
    .long 0

.globl path_stub
.p2align 4
.seh_proc path_stub
path_stub:
    sub rsp,0x28
    .seh_stackalloc 0x28
    .seh_endprologue
    mov [rsp+0x20],rdi
    call path_update
    add rsp,0x28
    ret
.seh_endproc

.globl capture_stub
.p2align 4
.seh_proc capture_stub
capture_stub:
    sub rsp,0xc8
    .seh_stackalloc 0xc8
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
    movdqu [rsp+0x80],xmm2
    movdqu [rsp+0x90],xmm3
    movdqu [rsp+0xa0],xmm4
    movdqu [rsp+0xb0],xmm5
    mov rcx,rdi
    mov rdx,r13
    call capture_analysis
    movdqu xmm0,[rsp+0x60]
    movdqu xmm1,[rsp+0x70]
    movdqu xmm2,[rsp+0x80]
    movdqu xmm3,[rsp+0x90]
    movdqu xmm4,[rsp+0xa0]
    movdqu xmm5,[rsp+0xb0]
    mov rax,[rsp+0x20]
    mov rcx,[rsp+0x28]
    mov rdx,[rsp+0x30]
    mov r8,[rsp+0x38]
    mov r9,[rsp+0x40]
    mov r10,[rsp+0x48]
    mov r11,[rsp+0x50]
    movsd xmm4,[r13]
    add rsp,0xc8
    ret
.seh_endproc

.globl original_prepare
.p2align 4
.seh_proc original_prepare
original_prepare:
    mov rax,rsp
    push rbp
    .seh_pushreg rbp
    push rbx
    .seh_pushreg rbx
    .seh_endprologue
.globl prepare_jump
prepare_jump:
    .byte 0xe9
    .long 0
.seh_endproc
.globl native_get_module
.p2align 4
native_get_module:
    .byte 0xff,0x25
    .long 0
.globl native_get_proc
.p2align 4
native_get_proc:
    .byte 0xff,0x25
    .long 0

.globl original_meter_prepare
.p2align 4
.seh_proc original_meter_prepare
original_meter_prepare:
    mov [rsp+0x18],rbx
    .seh_savereg rbx,0x18
    .seh_endprologue
.globl meter_prepare_jump
meter_prepare_jump:
    .byte 0xe9
    .long 0
.seh_endproc

.globl source_stub
.p2align 4
.seh_proc source_stub
source_stub:
    sub rsp,0xc8
    .seh_stackalloc 0xc8
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
    movdqu [rsp+0x80],xmm2
    movdqu [rsp+0x90],xmm3
    movdqu [rsp+0xa0],xmm4
    movdqu [rsp+0xb0],xmm5
    mov rcx,rdi
    mov edx,esi
    movaps xmm2,xmm7
    addsd xmm2,[rsp+0x80]
    movsd xmm3,[rsp+0x80]
    call source_update
    movsd [rsp+0x58],xmm0
    movdqu xmm0,[rsp+0x60]
    movdqu xmm1,[rsp+0x70]
    movdqu xmm2,[rsp+0x80]
    movdqu xmm3,[rsp+0x90]
    movdqu xmm4,[rsp+0xa0]
    movdqu xmm5,[rsp+0xb0]
    mov rax,[rsp+0x20]
    mov rcx,[rsp+0x28]
    mov rdx,[rsp+0x30]
    mov r8,[rsp+0x38]
    mov r9,[rsp+0x40]
    mov r10,[rsp+0x48]
    mov r11,[rsp+0x50]
    movsd xmm7,[rsp+0x58]
    lea rcx,[rdi+0xd68]
    add rsp,0xc8
    ret
.seh_endproc



.globl engine_tick_entry
.p2align 4
.seh_proc engine_tick_entry
engine_tick_entry:
    movsd [rcx+0x365b8],xmm2
    movsd [rcx+0x365c0],xmm3
    mov rax,[rsp+0x28]
    mov [rcx+0x365c8],rax
    mov rax,[rsp+0x30]
    mov [rcx+0x365d0],rax
    movzx eax,byte ptr [rsp+0x38]
    mov [rcx+0x365d8],al
    mov rax,rsp
    push rbp
    .seh_pushreg rbp
    push rbx
    .seh_pushreg rbx
    .seh_endprologue
.globl engine_tick_jump
engine_tick_jump:
    .byte 0xe9
    .long 0
.seh_endproc


