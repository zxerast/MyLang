;  MyLang runtime — работа со строками: lang_strlen

section .text

;  ──────────────────────────────────────────────────────────────
;  strlen (rdi = char*) → rax = длина без нуля
;  ──────────────────────────────────────────────────────────────
global lang_strlen
lang_strlen:
    xor rax, rax
.sl_loop:
    cmp byte [rdi+rax], 0
    je .sl_done
    inc rax
    jmp .sl_loop
.sl_done:
    ret
