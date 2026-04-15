;  MyLang runtime — базовые функции ввода/вывода, линкуется с пользовательским .o

section .rodata
s_true:  db "true", 10
s_false: db "false", 10
nl:      db 10

section .text

;  ──────────────────────────────────────────────────────────────
;  print_int (rdi = signed int64) — печатает число и \n в stdout
;  ──────────────────────────────────────────────────────────────
global print_int
print_int:
    push rbp
    mov rbp, rsp
    sub rsp, 32                     ;  буфер под 32 цифры/знак
    mov rax, rdi
    lea rcx, [rsp+31]
    mov byte [rcx], 10              ;  newline в конце
    mov rbx, 10
    xor r8, r8                      ;  флаг «отрицательное»
    test rax, rax
    jns .pi_conv
    mov r8, 1
    neg rax
.pi_conv:
    dec rcx
.pi_loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    mov [rcx], dl
    test rax, rax
    jz .pi_sign
    dec rcx
    jmp .pi_loop
.pi_sign:
    test r8, r8
    jz .pi_write
    dec rcx
    mov byte [rcx], '-'
.pi_write:
    mov rsi, rcx
    lea rdx, [rsp+32]
    sub rdx, rsi                    ;  длина
    mov rdi, 1
    mov rax, 1                      ;  sys_write
    syscall
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  print_string (rdi = char*) — печатает нул-терминированную строку и \n
;  ──────────────────────────────────────────────────────────────
global print_string
print_string:
    mov rsi, rdi                    ;  сохраним указатель
    xor rdx, rdx
.ps_len:
    cmp byte [rsi+rdx], 0
    je .ps_write
    inc rdx
    jmp .ps_len
.ps_write:
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, 1
    mov rdi, 1
    mov rsi, nl
    mov rdx, 1
    syscall
    ret

;  ──────────────────────────────────────────────────────────────
;  print_bool (rdi = 0/1)
;  ──────────────────────────────────────────────────────────────
global print_bool
print_bool:
    test rdi, rdi
    jz .pb_false
    mov rax, 1
    mov rdi, 1
    mov rsi, s_true
    mov rdx, 5
    syscall
    ret
.pb_false:
    mov rax, 1
    mov rdi, 1
    mov rsi, s_false
    mov rdx, 6
    syscall
    ret

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
;  panic (rdi = char*) — печатает сообщение и выходит с кодом 1
;  ──────────────────────────────────────────────────────────────
global lang_panic
lang_panic:
    call print_string
    mov rdi, 1
    mov rax, 60
    syscall

;  ──────────────────────────────────────────────────────────────
;  lang_alloc (rdi = size) → rax = указатель. Просто обёртка над brk.
;  ──────────────────────────────────────────────────────────────
section .bss
heap_ptr: resq 1                    ;  текущая вершина кучи
heap_end: resq 1                    ;  граница (увеличиваем через brk)

section .text
global lang_alloc
lang_alloc:
    push rbp
    mov rbp, rsp
    push rbx
    mov rbx, rdi                    ;  запрошенный размер

    ;  Если heap_ptr == 0 — инициализируем через brk(0)
    mov rax, [rel heap_ptr]
    test rax, rax
    jnz .la_check
    mov rax, 12                     ;  sys_brk
    xor rdi, rdi
    syscall
    mov [rel heap_ptr], rax
    mov [rel heap_end], rax

.la_check:
    mov rax, [rel heap_ptr]
    add rax, rbx                    ;  новая вершина после аллокации
    cmp rax, [rel heap_end]
    jbe .la_bump

    ;  Нужно расширить кучу: brk(new_end), округлив до 64 KiB
    mov rdi, rax
    add rdi, 0xFFFF
    and rdi, -0x10000
    mov rax, 12
    syscall
    mov [rel heap_end], rax

.la_bump:
    mov rax, [rel heap_ptr]
    add [rel heap_ptr], rbx         ;  двигаем указатель
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  lang_free — заглушка (bump-allocator без освобождения)
;  ──────────────────────────────────────────────────────────────
global lang_free
lang_free:
    ret
