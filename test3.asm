section .data
arr0: dd 8,3,1,54,72,12,1,6,3

section .text
extern print_int
extern print_string
extern print_bool
extern print_char
extern print_float
extern print_space
extern print_newline
extern lang_input
extern lang_strlen
extern lang_panic
extern lang_exit
extern lang_alloc
extern lang_free
extern lang_push
extern lang_pop

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    lea rax, [rel arr0]
    mov [rbp-36], rax
    mov rax, 1
    mov [rbp-48], rax
    mov rax, 0
    mov [rbp-56], rax
.while0:
    mov rax, [rbp-48]
    push rax
    lea rax, [rbp-36]
    mov rax, [rax + 8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile1
    mov rax, 0
    mov [rbp-56], rax
.while2:
    mov rax, [rbp-56]
    push rax
    lea rax, [rbp-36]
    mov rax, [rax + 8]
    push rax
    mov rax, [rbp-48]
    mov rbx, rax
    pop rax
    sub rax, rbx
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile3
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    push rax
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setg al
    movzx rax, al
    test rax, rax
    jz .endif5
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov [rbp-64], rax
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    push rax
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    pop rbx
    pop rcx
    mov [rcx + rbx*8], rax
    mov rax, [rbp-36]
    push rax
    mov rax, [rbp-56]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    push rax
    mov rax, [rbp-64]
    pop rbx
    pop rcx
    mov [rcx + rbx*8], rax
.endif5:
    inc qword [rbp-56]
    mov rax, [rbp-56]
    jmp .while2
.endwhile3:
    inc qword [rbp-48]
    mov rax, [rbp-48]
    jmp .while0
.endwhile1:
    mov rbx, [rbp-36]
    mov r13, [rbp-28]
    xor r12, r12
.print_arr_loop_6:
    cmp r12, r13
    je .print_arr_end_6
    mov rdi, [rbx + r12*8]
    call print_int
    inc r12
    cmp r12, r13
    je .print_arr_end_6
    call print_space
    jmp .print_arr_loop_6
.print_arr_end_6:
    call print_newline
    mov rax, 0
    jmp .end_of_main
.end_of_main:
    mov rsp, rbp
    pop rbp
    ret

global _start
_start:
    call main
    mov rdi, rax
    mov rax, 60
    syscall
