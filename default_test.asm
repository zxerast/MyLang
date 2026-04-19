section .rodata
str0: db ` `, 0
str1: db `_`, 0

section .bss
__default_Point_x: resq 1
__default_Point_y: resq 1

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
    sub rsp, 32
    mov rax, 10
    mov [rel __default_Point_x], rax
    mov rax, 20
    mov [rel __default_Point_y], rax
    mov rdi, 16
    call lang_alloc
    push rax
    mov rax, [rel __default_Point_x]
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, [rel __default_Point_y]
    mov rbx, [rsp]
    mov [rbx+8], rax
    pop rax
    mov [rbp-8], rax
    mov rax, [rbp-8]
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-8]
    mov rax, [rax+8]
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel str0]
    mov rdi, rax
    call print_string
    call print_newline
    mov rax, 4
    mov [rel __default_Point_x], rax
    mov rax, 7
    mov [rel __default_Point_y], rax
    mov rdi, 16
    call lang_alloc
    push rax
    mov rax, [rel __default_Point_x]
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, [rel __default_Point_y]
    mov rbx, [rsp]
    mov [rbx+8], rax
    pop rax
    mov [rbp-16], rax
    mov rax, [rbp-16]
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-16]
    mov rax, [rax+8]
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel str1]
    mov rdi, rax
    call print_string
    call print_newline
    mov rdi, 16
    call lang_alloc
    push rax
    mov rax, [rel __default_Point_x]
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, [rel __default_Point_y]
    mov rbx, [rsp]
    mov [rbx+8], rax
    pop rax
    mov [rbp-24], rax
    mov rax, [rbp-24]
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-24]
    mov rax, [rax+8]
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel str1]
    mov rdi, rax
    call print_string
    call print_newline
    mov rdi, 16
    call lang_alloc
    push rax
    mov rax, 99
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, [rel __default_Point_y]
    mov rbx, [rsp]
    mov [rbx+8], rax
    pop rax
    mov [rbp-32], rax
    mov rax, [rbp-32]
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-32]
    mov rax, [rax+8]
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel str1]
    mov rdi, rax
    call print_string
    call print_newline
    mov rdi, 16
    call lang_alloc
    push rax
    mov rax, [rel __default_Point_x]
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, [rel __default_Point_y]
    mov rbx, [rsp]
    mov [rbx+8], rax
    pop rax
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
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
