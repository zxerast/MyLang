section .rodata
__rt_div_zero: db "division by zero", 0
__rt_bounds:   db "array index out of bounds", 0
__rt_null_object: db "null object field access", 0
__rt_negative_pow: db "negative exponent", 0
str0: db `Earth is flat`, 0
str1: db `You are ok`, 0

section .text
extern print_int
extern print_string
extern print_bool
extern print_char
extern print_float
extern print_space
extern print_newline
extern lang_input
extern lang_parse_input_int
extern lang_parse_input_float
extern lang_parse_input_char
extern lang_input_array_fixed
extern lang_input_array_dyn
extern lang_strlen
extern lang_panic
extern lang_exit
extern lang_alloc
extern lang_push_sized
extern lang_pop_sized
extern lang_strcat
extern lang_streq

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov rax, 2
    push rax
    mov rax, 2
    mov rbx, rax
    pop rax
    add rax, rbx
    push rax
    mov rax, 5
    mov rbx, rax
    pop rax
    cmp rax, rbx
    sete al
    movzx rax, al
    test rax, rax
    jz .else0
    lea rax, [rel str0]
    mov rdi, rax
    mov [rbp-8], rsp
    and rsp, -16
    call print_string
    mov rsp, [rbp-8]
    mov [rbp-8], rsp
    and rsp, -16
    call print_newline
    mov rsp, [rbp-8]
    jmp .endif1
.else0:
    lea rax, [rel str1]
    mov rdi, rax
    mov [rbp-8], rsp
    and rsp, -16
    call print_string
    mov rsp, [rbp-8]
    mov [rbp-8], rsp
    and rsp, -16
    call print_newline
    mov rsp, [rbp-8]
.endif1:
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

section .note.GNU-stack noalloc noexec nowrite progbits
