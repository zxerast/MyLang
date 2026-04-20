section .rodata
__rt_div_zero: db "division by zero", 0
__rt_bounds:   db "array index out of bounds", 0
str0: db `hello`, 0

section .bss
__global_counter: resb 8
__global_greeting: resb 8
__global_buffer: resb 24

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

global bump
bump:
    push rbp
    mov rbp, rsp
    mov rax, [rel __global_counter]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov [rel __global_counter], rax
    mov rax, [rel __global_counter]
    jmp .end_of_bump
.end_of_bump:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    mov rax, 42
    mov [rel __global_counter], rax
    lea rax, [rel str0]
    mov [rel __global_greeting], rax
    mov rdi, 24
    call lang_alloc
    mov [rel __global_buffer], rax
    mov qword [rel __global_buffer + 8], 3
    mov qword [rel __global_buffer + 16], 3
    push qword [rel __global_buffer]
    mov rax, 1
    pop rbx
    mov [rbx + 0], rax
    push qword [rel __global_buffer]
    mov rax, 2
    pop rbx
    mov [rbx + 8], rax
    push qword [rel __global_buffer]
    mov rax, 3
    pop rbx
    mov [rbx + 16], rax
    mov rax, [rel __global_counter]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rel __global_greeting]
    mov rdi, rax
    call print_string
    call print_newline
    lea rax, [rel __global_buffer]
    push rax
    mov rax, 0
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok0
    lea rdi, [rel __rt_bounds]
    mov rsi, 14
    call lang_panic
.bndok0:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    push rax
    lea rax, [rel __global_buffer]
    push rax
    mov rax, 2
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok1
    lea rdi, [rel __rt_bounds]
    mov rsi, 14
    call lang_panic
.bndok1:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel __global_buffer]
    mov rax, [rax + 8]
    mov rdi, rax
    call print_int
    call print_newline
    inc qword [rel __global_counter]
    mov rax, [rel __global_counter]
    mov rax, [rel __global_counter]
    mov rdi, rax
    call print_int
    call print_newline
    call bump
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rel __global_counter]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 99
    push rax
    lea rdi, [rel __global_buffer]
    pop rsi
    call lang_push
    lea rax, [rel __global_buffer]
    push rax
    mov rax, 3
    mov rbx, rax
    pop rax
    mov rcx, [rax + 8]
    cmp rbx, rcx
    jb .bndok2
    lea rdi, [rel __rt_bounds]
    mov rsi, 21
    call lang_panic
.bndok2:
    mov rax, [rax]
    mov rax, [rax + rbx*8]
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel __global_buffer]
    mov rax, [rax + 8]
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
