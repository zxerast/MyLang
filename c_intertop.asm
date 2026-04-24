section .data
arr0: dd 1,2

section .rodata
__rt_div_zero: db "division by zero", 0
__rt_bounds:   db "array index out of bounds", 0
str0: db `Hello C`, 0
str1: db `Simulated pointer: `, 0
str2: db `Inferring types...`, 0
str3: db `Some text`, 0
str4: db `NULL`, 0
str5: db `Cast string to bool: `, 0
str6: db ` and `, 0

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

global testCInterop
testCInterop:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    lea rax, [rel str0]
    mov [rbp-8], rax
    mov rax, 1024
    mov [rbp-16], rax
    lea rax, [rel str1]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-16]
    mov rdi, rax
    call print_int
    call print_newline
.end_of_testCInterop:
    mov rsp, rbp
    pop rbp
    ret

global testRuntimeErrors
testRuntimeErrors:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rdi
    mov rax, [rbp-8]
    xor rax, 1
    test rax, rax
    jz .endif1
    jmp .end_of_testRuntimeErrors
.endif1:
    mov rax, 0
    mov [rbp-16], rax
    mov rax, 10
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    test rbx, rbx
    jnz .divok2
    lea rdi, [rel __rt_div_zero]
    mov rsi, 20
    call lang_panic
.divok2:
    cqo
    idiv rbx
    mov [rbp-24], rax
    lea rax, [rel arr0]
    mov [rbp-32], rax
    mov rax, [rbp-32]
    push rax
    mov rax, 5
    mov rbx, rax
    pop rax
    cmp rbx, 2
    jb .bndok3
    lea rdi, [rel __rt_bounds]
    mov rsi, 24
    call lang_panic
.bndok3:
    movsxd rax, dword [rax + rbx*4]
    mov [rbp-40], rax
.end_of_testRuntimeErrors:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    call testCInterop
    lea rax, [rel str2]
    mov [rbp-8], rax
    mov rax, [rbp-8]
    mov rdi, rax
    call print_string
    call print_newline
    mov rax, 4614253070214989087
    mov [rbp-16], rax
    mov rax, [rbp-16]
    movq xmm0, rax
    cvttsd2si rax, xmm0
    mov [rbp-24], rax
    lea rax, [rel str3]
    mov [rbp-32], rax
    lea rax, [rel str4]
    mov [rbp-40], rax
    lea rax, [rel str5]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-32]
    mov rdi, rax
    call print_bool
    call print_space
    lea rax, [rel str6]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-40]
    mov rdi, rax
    call print_bool
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
