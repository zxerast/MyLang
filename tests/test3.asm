section .rodata
__rt_div_zero: db "division by zero", 0
__rt_bounds:   db "array index out of bounds", 0

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
extern lang_strcat

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov rdi, 72
    call lang_alloc
    mov [rbp-24], rax
    mov qword [rbp-16], 9
    mov qword [rbp-8], 9
    push qword [rbp-24]
    mov rax, 8
    pop rbx
    mov [rbx + 0], rax
    push qword [rbp-24]
    mov rax, 3
    pop rbx
    mov [rbx + 8], rax
    push qword [rbp-24]
    mov rax, 1
    pop rbx
    mov [rbx + 16], rax
    push qword [rbp-24]
    mov rax, 54
    pop rbx
    mov [rbx + 24], rax
    push qword [rbp-24]
    mov rax, 72
    pop rbx
    mov [rbx + 32], rax
    push qword [rbp-24]
    mov rax, 12
    pop rbx
    mov [rbx + 40], rax
    push qword [rbp-24]
    mov rax, 1
    pop rbx
    mov [rbx + 48], rax
    push qword [rbp-24]
    mov rax, 6
    pop rbx
    mov [rbx + 56], rax
    push qword [rbp-24]
    mov rax, 3
    pop rbx
    mov [rbx + 64], rax
    mov rax, 1
    mov [rbp-32], rax
    mov rax, 0
    mov [rbp-40], rax
.while0:
    mov rax, [rbp-32]
    push rax
    lea rax, [rbp-24]
    mov rax, [rax + 8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile1
    mov rax, 0
    mov [rbp-40], rax
.while2:
    mov rax, [rbp-40]
    push rax
    lea rax, [rbp-24]
    mov rax, [rax + 8]
    push rax
    mov rax, [rbp-32]
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
    lea rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok6
    lea rdi, [rel __rt_bounds]
    mov rsi, 9
    call lang_panic
.bndok6:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    push rax
    lea rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok7
    lea rdi, [rel __rt_bounds]
    mov rsi, 9
    call lang_panic
.bndok7:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setg al
    movzx rax, al
    test rax, rax
    jz .endif5
    lea rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok8
    lea rdi, [rel __rt_bounds]
    mov rsi, 10
    call lang_panic
.bndok8:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    mov [rbp-48], rax
    mov rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    push rax
    lea rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok9
    lea rdi, [rel __rt_bounds]
    mov rsi, 11
    call lang_panic
.bndok9:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    pop rbx
    pop rcx
    mov [rcx + rbx*8], rax
    mov rax, [rbp-24]
    push rax
    mov rax, [rbp-40]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    push rax
    mov rax, [rbp-48]
    pop rbx
    pop rcx
    mov [rcx + rbx*8], rax
.endif5:
    inc qword [rbp-40]
    mov rax, [rbp-40]
    jmp .while2
.endwhile3:
    inc qword [rbp-32]
    mov rax, [rbp-32]
    jmp .while0
.endwhile1:
    mov rbx, [rbp-24]
    mov r13, [rbp-16]
    xor r12, r12
.print_arr_loop_10:
    cmp r12, r13
    je .print_arr_end_10
    mov rdi, [rbx + r12*8]
    call print_int
    inc r12
    cmp r12, r13
    je .print_arr_end_10
    call print_space
    jmp .print_arr_loop_10
.print_arr_end_10:
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
