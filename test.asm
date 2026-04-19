section .data
arr0: dq 10,20,30

section .rodata
str0: db `Hello, `, 0
str1: db `World!`, 0
str2: db `x `, 208, ``, 177, ``, 208, ``, 190, ``, 208, ``, 187, ``, 209, ``, 140, ``, 209, ``, 136, ``, 208, ``, 181, ` 5`, 0
str3: db `x <= 5`, 0
str4: db `add `, 209, ``, 128, ``, 208, ``, 176, ``, 208, ``, 177, ``, 208, ``, 190, ``, 209, ``, 130, ``, 208, ``, 176, ``, 208, ``, 181, ``, 209, ``, 130, `!`, 0

section .text
extern print_int
extern print_string
extern print_bool
extern print_space
extern print_newline
extern input
extern strlen
extern panic
extern exit
extern alloc
extern free
extern push
extern pop

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov rax, 10
    mov [rbp-8], rax
    mov rax, 3
    mov [rbp-16], rax
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    imul rax, rbx
    push rax
    mov rax, 1
    mov rbx, rax
    pop rax
    sub rax, rbx
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    cqo
    idiv rbx
    mov rax, rdx
    mov rdi, rax
    call print_int
    call print_newline
    lea rax, [rel str0]
    mov [rbp-24], rax
    lea rax, [rel str1]
    mov [rbp-32], rax
    mov rax, [rbp-24]
    push rax
    mov rax, [rbp-32]
    mov rbx, rax
    pop rax
    add rax, rbx
    mov rdi, rax
    call print_string
    call print_newline
    mov rax, 1
    mov [rbp-40], rax
    mov rax, [rbp-40]
    mov rdi, rax
    call print_bool
    call print_newline
    mov rax, [rbp-8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setg al
    movzx rax, al
    mov rdi, rax
    call print_bool
    call print_newline
    mov rax, [rbp-8]
    push rax
    mov rax, 10
    mov rbx, rax
    pop rax
    cmp rax, rbx
    sete al
    movzx rax, al
    mov rdi, rax
    call print_bool
    call print_newline
    mov rax, [rbp-8]
    push rax
    mov rax, 5
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setg al
    movzx rax, al
    test rax, rax
    jz .else0
    lea rax, [rel str2]
    mov rdi, rax
    call print_string
    call print_newline
    jmp .endif1
.else0:
    lea rax, [rel str3]
    mov rdi, rax
    call print_string
    call print_newline
.endif1:
    mov rax, 0
    mov [rbp-48], rax
.while2:
    mov rax, [rbp-48]
    push rax
    mov rax, 5
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile3
    mov rax, [rbp-48]
    mov rdi, rax
    call print_int
    call print_newline
    inc qword [rbp-48]
    mov rax, [rbp-48]
    jmp .while2
.endwhile3:
    lea rax, [rel arr0]
    mov [rbp-60], rax
    mov rax, [rbp-60]
    push rax
    mov rax, 0
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-60]
    push rax
    mov rax, 2
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, [rbp-52]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 4
    push rax
    mov rax, 3
    push rax
    pop rdi
    pop rsi
    call lang_add
    mov [rbp-72], rax
    mov rax, [rbp-72]
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 5
    push rax
    mov rax, 4
    push rax
    pop rdi
    pop rsi
    call lang_add
    mov rdi, rax
    call print_int
    call print_newline
    mov rax, 4
    push rax
    mov rax, 3
    push rax
    pop rdi
    pop rsi
    call lang_add
    push rax
    mov rax, 7
    mov rbx, rax
    pop rax
    cmp rax, rbx
    sete al
    movzx rax, al
    test rax, rax
    jz .endif5
    lea rax, [rel str4]
    mov rdi, rax
    call print_string
    call print_newline
.endif5:
    mov rax, 0
    jmp .end_of_main
.end_of_main:
    mov rsp, rbp
    pop rbp
    ret

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

global _start
_start:
    call main
    mov rdi, rax
    mov rax, 60
    syscall
