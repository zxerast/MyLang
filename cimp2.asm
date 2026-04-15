section .rodata
str0: db 104,101,108,108,111,32,37,100,92,110,0

section .text
extern print_int
extern print_string
extern print_bool
extern lang_strlen
extern lang_panic
extern lang_alloc
extern lang_free

global _start
_start:
    call lang_main
    mov rdi, rax
    mov rax, 60
    syscall

global lang_main
lang_main:
    push rbp
    mov rbp, rsp
    mov rax, 42
    push rax
    lea rax, [rel str0]
    push rax
    pop rdi
    pop rsi
    call lang_printf
    mov rax, 0
    jmp .epilog_main
.epilog_main:
    mov rsp, rbp
    pop rbp
    ret

