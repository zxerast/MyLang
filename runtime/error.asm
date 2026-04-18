;  MyLang runtime — обработка ошибок: lang_panic

section .text

extern print_string
extern print_newline

;  ──────────────────────────────────────────────────────────────
;  panic (rdi = char*) — печатает сообщение с \n и выходит с кодом 1
;  ──────────────────────────────────────────────────────────────
global lang_panic
lang_panic:
    call print_string
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
