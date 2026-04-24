section .data
arr0: dq 0,0

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
extern lang_input_int
extern lang_input_float
extern lang_input_bool
extern lang_input_char
extern lang_strlen
extern lang_panic
extern lang_exit
extern lang_alloc
extern lang_free
extern lang_push
extern lang_pop
extern lang_strcat
extern lang_streq

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov rax, 4
    mov [rbp-16], rax
    lea rax, [rel arr0]
    mov [rbp-48], rax
    mov rax, [rbp-48]
    mov rbx, rax
    mov r13, 2
    xor r12, r12
.print_arr_loop_0:
    cmp r12, r13
    je .print_arr_end_0
    mov rdi, [rbx + r12*8]
    call print_int
    inc r12
    cmp r12, r13
    je .print_arr_end_0
    call print_space
    jmp .print_arr_loop_0
.print_arr_end_0:
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
