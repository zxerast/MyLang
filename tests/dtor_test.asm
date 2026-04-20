section .rodata
__rt_div_zero: db "division by zero", 0
__rt_bounds:   db "array index out of bounds", 0
str0: db `ctor`, 0
str1: db `dtor`, 0
str2: db `=== scopeExit ===`, 0
str3: db `=== explicitDelete ===`, 0
str4: db `=== done ===`, 0

section .bss
__default_instance_Resource: resb 8

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

global Resource_Resource
Resource_Resource:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov rax, [rbp-16]
    mov rbx, [rbp-8]
    mov [rbx + 0], rax
    lea rax, [rel str0]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-8]
    mov rax, [rax + 0]
    mov rdi, rax
    call print_int
    call print_newline
.end_of_Resource_Resource:
    mov rsp, rbp
    pop rbp
    ret

global Resource_dtor
Resource_dtor:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rdi
    lea rax, [rel str1]
    mov rdi, rax
    call print_string
    call print_space
    mov rax, [rbp-8]
    mov rax, [rax + 0]
    mov rdi, rax
    call print_int
    call print_newline
.end_of_Resource_dtor:
    mov rsp, rbp
    pop rbp
    ret

global scopeExit
scopeExit:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov qword [rbp-8], 0
    mov rdi, 8
    call lang_alloc
    push rax
    mov rax, [rel __default_instance_Resource + 0]
    mov rbx, [rsp]
    mov [rbx + 0], rax
    mov rax, 1
    push rax
    pop rsi
    mov rdi, [rsp]
    call Resource_Resource
    pop rax
    mov [rbp-8], rax
    mov qword [rbp-16], 0
    mov rdi, 8
    call lang_alloc
    push rax
    mov rax, [rel __default_instance_Resource + 0]
    mov rbx, [rsp]
    mov [rbx + 0], rax
    mov rax, 2
    push rax
    pop rsi
    mov rdi, [rsp]
    call Resource_Resource
    pop rax
    mov [rbp-16], rax
.end_of_scopeExit:
    sub rsp, 16
    mov [rsp], rax
    mov rdi, [rbp-16]
    test rdi, rdi
    jz .no_dtor0
    call Resource_dtor
.no_dtor0:
    mov rdi, [rbp-8]
    test rdi, rdi
    jz .no_dtor1
    call Resource_dtor
.no_dtor1:
    mov rax, [rsp]
    add rsp, 16
    mov rsp, rbp
    pop rbp
    ret

global explicitDelete
explicitDelete:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov qword [rbp-8], 0
    mov rdi, 8
    call lang_alloc
    push rax
    mov rax, [rel __default_instance_Resource + 0]
    mov rbx, [rsp]
    mov [rbx + 0], rax
    mov rax, 10
    push rax
    pop rsi
    mov rdi, [rsp]
    call Resource_Resource
    pop rax
    mov [rbp-8], rax
    mov rax, [rbp-8]
    sub rsp, 16
    mov [rsp], rax
    mov rdi, rax
    call Resource_dtor
    mov rdi, [rsp]
    add rsp, 16
    call lang_free
    mov qword [rbp-8], 0
.end_of_explicitDelete:
    sub rsp, 16
    mov [rsp], rax
    mov rdi, [rbp-8]
    test rdi, rdi
    jz .no_dtor2
    call Resource_dtor
.no_dtor2:
    mov rax, [rsp]
    add rsp, 16
    mov rsp, rbp
    pop rbp
    ret

global main
main:
    push rbp
    mov rbp, rsp
    mov rax, 0
    mov [rel __default_instance_Resource + 0], rax
    lea rax, [rel str2]
    mov rdi, rax
    call print_string
    call print_newline
    call scopeExit
    lea rax, [rel str3]
    mov rdi, rax
    call print_string
    call print_newline
    call explicitDelete
    lea rax, [rel str4]
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
