section .text
extern print_int
extern print_string
extern print_bool
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

global add
add:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    add rax, rbx
    jmp .end_of_add
.end_of_add:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    mov rax, 4
    push rax
    mov rax, 3
    push rax
    pop rdi
    pop rsi
    call add
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 200
    push rax
    mov rax, 100
    push rax
    pop rdi
    pop rsi
    call add
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
