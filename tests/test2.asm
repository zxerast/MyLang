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
    sub rsp, 32
    mov rdi, 16
    call lang_alloc
    push rax
    mov qword [rax + 0], 0
    mov qword [rax + 8], 0
    mov rax, 5
    mov rbx, [rsp]
    mov [rbx+0], rax
    mov rax, 10
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
    mov rax, 0
    mov [rbp-16], rax
.while0:
    mov rax, [rbp-16]
    push rax
    mov rax, 10
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile1
    mov rax, [rbp-16]
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    add rax, rbx
    mov [rbp-16], rax
    mov rax, [rbp-16]
    push rax
    mov rax, 2
    mov rbx, rax
    pop rax
    test rbx, rbx
    jnz .modok4
    lea rdi, [rel __rt_div_zero]
    mov rsi, 16
    call lang_panic
.modok4:
    cqo
    idiv rbx
    mov rax, rdx
    push rax
    mov rax, 0
    mov rbx, rax
    pop rax
    cmp rax, rbx
    sete al
    movzx rax, al
    test rax, rax
    jz .endif3
    jmp .while0
.endif3:
    mov rax, [rbp-16]
    push rax
    mov rax, 7
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setg al
    movzx rax, al
    test rax, rax
    jz .endif6
    jmp .endwhile1
.endif6:
    mov rax, [rbp-16]
    mov rdi, rax
    call print_int
    call print_newline
    jmp .while0
.endwhile1:
    mov rax, 42
    cvtsi2sd xmm0, rax
    movq rax, xmm0
    mov [rbp-24], rax
    mov rax, [rbp-24]
    movq xmm0, rax
    call print_float
    call print_newline
    mov rax, 5
    push rax
    pop rdi
    call square
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 0
    jmp .end_of_main
.end_of_main:
    mov rsp, rbp
    pop rbp
    ret

global square
square:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rdi
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-8]
    mov rbx, rax
    pop rax
    imul rax, rbx
    jmp .end_of_square
.end_of_square:
    mov rsp, rbp
    pop rbp
    ret

global _start
_start:
    call main
    mov rdi, rax
    mov rax, 60
    syscall
