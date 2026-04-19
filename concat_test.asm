section .rodata
str0: db `World`, 0

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
    lea rax, [rel str0]
    mov [rbp-8], rax
    mov rax, 33
    mov [rbp-16], rax
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    and rax, 0xFF
    push rax
    mov rsi, rsp
    mov rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    mov [rbp-24], rax
    mov rax, [rbp-24]
    mov rdi, rax
    call print_string
    call print_newline
    mov rax, [rbp-16]
    and rax, 0xFF
    push rax
    mov rax, [rbp-8]
    push rax
    mov rsi, [rsp]
    lea rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    mov [rbp-32], rax
    mov rax, [rbp-32]
    mov rdi, rax
    call print_string
    call print_newline
    mov rax, [rbp-16]
    and rax, 0xFF
    push rax
    mov rax, [rbp-8]
    push rax
    mov rsi, [rsp]
    lea rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    push rax
    mov rax, [rbp-16]
    and rax, 0xFF
    push rax
    mov rsi, rsp
    mov rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    mov [rbp-40], rax
    mov rax, [rbp-40]
    mov rdi, rax
    call print_string
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
