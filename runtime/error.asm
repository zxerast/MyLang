;  MyLang runtime — обработка ошибок: lang_panic

section .rodata
__rt_prefix: db "runtime error: ", 0
__rt_atline: db " at line ", 0

section .text

extern print_string
extern print_int
extern print_newline

;  ──────────────────────────────────────────────────────────────
;  panic (rdi = char*, rsi = номер строки)
;  Печатает "runtime error: <msg> at line <N>\n" и exit(1).
;  ──────────────────────────────────────────────────────────────
global lang_panic
lang_panic:
    push rbp
    mov rbp, rsp
    sub rsp, 16                     ;  [rbp-8]=msg, [rbp-16]=line; rsp 16-aligned
    mov [rbp-8], rdi
    mov [rbp-16], rsi

    lea rdi, [rel __rt_prefix]
    call print_string

    mov rdi, [rbp-8]
    call print_string

    lea rdi, [rel __rt_atline]
    call print_string

    mov rdi, [rbp-16]
    call print_int

    call print_newline

    mov rdi, 1
    mov rax, 60
    syscall

;  ──────────────────────────────────────────────────────────────
;  exit (rdi = int32 код возврата) — sys_exit, не возвращается
;  ──────────────────────────────────────────────────────────────
global lang_exit
lang_exit:
    mov rax, 60
    syscall
