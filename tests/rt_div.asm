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
    sub rsp, 16
    mov rax, 10
    mov [rbp-8], rax
    mov rax, 0
    mov [rbp-16], rax
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    test rbx, rbx
    jnz .divok0
    lea rdi, [rel __rt_div_zero]
    mov rsi, 4
    call lang_panic
.divok0:
    cqo
    idiv rbx
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
