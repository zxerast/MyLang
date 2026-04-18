;  MyLang runtime — управление памятью: lang_alloc, lang_free

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
