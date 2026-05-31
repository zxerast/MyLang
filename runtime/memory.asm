;  MyLang runtime — управление памятью: lang_alloc, lang_free

;  ──────────────────────────────────────────────────────────────
;  lang_alloc (rdi = size) → rax = указатель.
;  Bump-аллокатор поверх mmap: не трогает brk, чтобы не конфликтовать
;  с malloc/glibc при FFI-вызовах вроде fopen/fprintf/fclose.
;  ──────────────────────────────────────────────────────────────
section .bss
heap_ptr: resq 1                    ;  текущая вершина кучи
heap_end: resq 1                    ;  граница текущего mmap-чанка

section .text
global lang_alloc
lang_alloc:
    push rbp
    mov rbp, rsp
    push rbx
    push r12

    ;  Округляем размер выделения до 16 байт. Это сохраняет выравнивание
    ;  последующих указателей даже после byte/int-буферов.
    mov rbx, rdi
    add rbx, 15
    and rbx, -16
    test rbx, rbx
    jnz .la_check
    mov rbx, 16

.la_check:
    mov rax, [rel heap_ptr]
    mov r12, rax
    add r12, rbx                    ;  новая вершина после аллокации
    cmp r12, [rel heap_end]
    jbe .la_bump

    ;  Нужен новый mmap-чанк. Размер чанка: max(request, 64 KiB),
    ;  округлённый до 64 KiB. Старый хвост просто остаётся неиспользованным.
    mov rsi, rbx
    cmp rsi, 0x10000
    jae .la_round_chunk
    mov rsi, 0x10000
.la_round_chunk:
    add rsi, 0xFFFF
    and rsi, -0x10000

    mov rax, 9                      ;  sys_mmap
    xor rdi, rdi
    mov rdx, 3                      ;  PROT_READ | PROT_WRITE
    mov r10, 0x22                   ;  MAP_PRIVATE | MAP_ANONYMOUS
    mov r8, -1
    xor r9, r9
    syscall
    mov [rel heap_ptr], rax
    lea r12, [rax + rsi]
    mov [rel heap_end], r12

.la_bump:
    mov rax, [rel heap_ptr]
    add [rel heap_ptr], rbx         ;  двигаем указатель
    pop r12
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

section .note.GNU-stack noalloc noexec nowrite progbits
