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
    sub rsp, 32
    mov rdi, 24
    call lang_alloc
    mov [rbp-24], rax
    mov qword [rbp-16], 3
    mov qword [rbp-8], 3
    push qword [rbp-24]
    mov rax, 10
    pop rbx
    mov [rbx + 0], rax
    push qword [rbp-24]
    mov rax, 20
    pop rbx
    mov [rbx + 8], rax
    push qword [rbp-24]
    mov rax, 30
    pop rbx
    mov [rbx + 16], rax
    mov rax, 0
    mov [rbp-32], rax
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
    mov rax, [rbp-24]
    push rax
    mov rax, [rbp-32]
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov rdi, rax
    call print_int
    call print_newline
    inc qword [rbp-32]
    mov rax, [rbp-32]
    jmp .while0
.endwhile1:
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
