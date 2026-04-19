section .rodata
str0: db `->`, 0
str1: db `Me`, 0
str2: db `You`, 0
str3: db `Him`, 0
str4: db `Hello`, 0
str5: db ` World `, 0

section .bss
__default_instance_MyClass: resb 32

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

global MyClass_Add
MyClass_Add:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov [rbp-24], rdx
    mov rax, [rbp-8]
    mov rax, [rax + 0]
    push rax
    mov rax, [rbp-16]
    pop rbx
    mov [rbx+0], rax
    mov rax, [rbp-8]
    mov rax, [rax + 0]
    push rax
    mov rax, [rbp-24]
    pop rbx
    mov [rbx+8], rax
    mov rax, [rbp-8]
    mov rax, [rax + 0]
    push rax
    mov rdi, [rbp-8]
    add rdi, 8
    pop rsi
    call lang_push
    mov rdi, 16
    call lang_alloc
    push rax
    pop rax
    mov rbx, [rbp-8]
    mov [rbx + 0], rax
.end_of_MyClass_Add:
    mov rsp, rbp
    pop rbp
    ret

global MyClass_Print
MyClass_Print:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rdi
    mov rax, 0
    mov [rbp-16], rax
.while0:
    mov rax, [rbp-16]
    push rax
    mov rax, [rbp-8]
    add rax, 8
    mov rax, [rax + 8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile1
    mov rax, [rbp-8]
    mov rax, [rax + 8]
    push rax
    mov rax, [rbp-16]
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov [rbp-24], rax
    mov rax, [rbp-24]
    mov rax, [rax+0]
    mov rdi, rax
    call print_int
    call print_space
    lea rax, [rel str0]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-24]
    mov rax, [rax+8]
    mov rdi, rax
    call print_string
    call print_newline
    inc qword [rbp-16]
    mov rax, [rbp-16]
    jmp .while0
.endwhile1:
.end_of_MyClass_Print:
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov rdi, 16
    call lang_alloc
    push rax
    pop rax
    mov [rel __default_instance_MyClass + 0], rax
    mov rdi, 32
    call lang_alloc
    push rax
    mov rax, [rel __default_instance_MyClass + 0]
    mov rbx, [rsp]
    mov [rbx + 0], rax
    mov rax, [rel __default_instance_MyClass + 8]
    mov rbx, [rsp]
    mov [rbx + 8], rax
    mov rax, [rel __default_instance_MyClass + 16]
    mov rbx, [rsp]
    mov [rbx + 16], rax
    mov rax, [rel __default_instance_MyClass + 24]
    mov rbx, [rsp]
    mov [rbx + 24], rax
    pop rax
    mov [rbp-8], rax
    mov rax, [rbp-8]
    push rax
    lea rax, [rel str1]
    push rax
    mov rax, 1
    push rax
    pop rsi
    pop rdx
    pop rdi
    call MyClass_Add
    mov rax, [rbp-8]
    push rax
    lea rax, [rel str2]
    push rax
    mov rax, 2
    push rax
    pop rsi
    pop rdx
    pop rdi
    call MyClass_Add
    mov rax, [rbp-8]
    push rax
    lea rax, [rel str3]
    push rax
    mov rax, 3
    push rax
    pop rsi
    pop rdx
    pop rdi
    call MyClass_Add
    mov rax, [rbp-8]
    push rax
    pop rdi
    call MyClass_Print
    lea rax, [rel __default_instance_MyClass]
    push rax
    pop rdi
    call MyClass_Print
    mov rdi, 24
    call lang_alloc
    mov [rbp-32], rax
    mov qword [rbp-24], 3
    mov qword [rbp-16], 3
    push qword [rbp-32]
    mov rax, 10
    pop rbx
    mov [rbx + 0], rax
    push qword [rbp-32]
    mov rax, 20
    pop rbx
    mov [rbx + 8], rax
    push qword [rbp-32]
    mov rax, 30
    pop rbx
    mov [rbx + 16], rax
    mov rax, 0
    mov [rbp-40], rax
.while2:
    mov rax, [rbp-40]
    push rax
    lea rax, [rbp-32]
    mov rax, [rax + 8]
    mov rbx, rax
    pop rax
    cmp rax, rbx
    setl al
    movzx rax, al
    test rax, rax
    jz .endwhile3
    mov rax, [rbp-32]
    push rax
    mov rax, [rbp-40]
    mov rbx, rax
    pop rax
    mov rax, [rax + rbx*8]
    mov rdi, rax
    call print_int
    call print_newline
    inc qword [rbp-40]
    mov rax, [rbp-40]
    jmp .while2
.endwhile3:
    mov rbx, [rbp-32]
    mov r13, [rbp-24]
    xor r12, r12
.print_arr_loop_4:
    cmp r12, r13
    je .print_arr_end_4
    mov rdi, [rbx + r12*8]
    call print_int
    inc r12
    cmp r12, r13
    je .print_arr_end_4
    call print_space
    jmp .print_arr_loop_4
.print_arr_end_4:
    call print_newline
    lea rax, [rel str4]
    mov [rbp-48], rax
    mov rax, [rbp-48]
    push rax
    lea rax, [rel str5]
    push rax
    mov rsi, [rsp]
    mov rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    mov [rbp-56], rax
    mov rax, 33
    mov [rbp-64], rax
    mov rax, [rbp-56]
    push rax
    mov rax, [rbp-64]
    and rax, 0xFF
    push rax
    mov rsi, rsp
    mov rdi, [rsp+8]
    call lang_strcat
    add rsp, 16
    mov rdi, rax
    call print_string
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
