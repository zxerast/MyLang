;  MyLang runtime — работа со строками: lang_strlen, lang_strcat

extern lang_alloc

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

;  ──────────────────────────────────────────────────────────────
;  strcat (rdi = left, rsi = right) → rax = указатель на новую строку
;  Аллоцирует len(left) + len(right) + 1 байт, копирует оба и ставит \0.
;  ──────────────────────────────────────────────────────────────
global lang_strcat
lang_strcat:
    push rbp
    mov rbp, rsp
    push rbx                        ;  callee-saved
    push r12                        ;  left
    push r13                        ;  right
    push r14                        ;  len(left)
    push r15                        ;  len(right)
    sub rsp, 8                      ;  выравнивание до 16

    mov r12, rdi
    mov r13, rsi

    ;  len(left)
    xor rax, rax
.sc_l_loop:
    cmp byte [r12+rax], 0
    je .sc_l_done
    inc rax
    jmp .sc_l_loop
.sc_l_done:
    mov r14, rax

    ;  len(right)
    xor rax, rax
.sc_r_loop:
    cmp byte [r13+rax], 0
    je .sc_r_done
    inc rax
    jmp .sc_r_loop
.sc_r_done:
    mov r15, rax

    ;  alloc len(left) + len(right) + 1
    mov rdi, r14
    add rdi, r15
    add rdi, 1
    call lang_alloc
    mov rbx, rax                    ;  rbx = буфер результата

    ;  Копируем left
    xor rcx, rcx
.sc_cp_l:
    cmp rcx, r14
    je .sc_cp_l_done
    mov dl, [r12 + rcx]
    mov [rbx + rcx], dl
    inc rcx
    jmp .sc_cp_l
.sc_cp_l_done:

    ;  Копируем right сразу после left (адрес назначения = rbx + r14, держим в rdi)
    lea rdi, [rbx + r14]
    xor rcx, rcx
.sc_cp_r:
    cmp rcx, r15
    je .sc_cp_r_done
    mov dl, [r13 + rcx]
    mov [rdi + rcx], dl
    inc rcx
    jmp .sc_cp_r
.sc_cp_r_done:

    ;  Терминатор
    mov byte [rdi + r15], 0

    mov rax, rbx
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
