;  MyLang runtime — динамические массивы: lang_push, lang_pop
;  DynArray layout (24 байта): [arr+0]=ptr, [arr+8]=len, [arr+16]=cap

section .rodata
pop_empty_msg: db "runtime: pop from empty array", 0

section .text

extern lang_alloc
extern lang_panic

;  ──────────────────────────────────────────────────────────────
;  push (rdi = DynArray*, rsi = элемент 8 байт)
;  При len == cap буфер растёт: new_cap = cap==0 ? 8 : cap*2.
;  ──────────────────────────────────────────────────────────────
global lang_push
lang_push:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    sub rsp, 8                      ;  выравнивание стека до 16 байт перед call

    mov rbx, rdi                    ;  rbx = arr*
    mov r12, rsi                    ;  r12 = сохранённый элемент (переживёт call)

    mov rax, [rbx+8]                ;  len
    cmp rax, [rbx+16]               ;  len == cap?
    jne .lp_store

    ;  Расчёт новой ёмкости
    mov r13, [rbx+16]
    test r13, r13
    jnz .lp_double
    mov r13, 8
    jmp .lp_alloc
.lp_double:
    shl r13, 1

.lp_alloc:
    mov rdi, r13
    shl rdi, 3                      ;  new_cap * 8 байт
    call lang_alloc                 ;  rax = new_ptr

    ;  Копируем старые len qword'ов в новый буфер
    mov rcx, [rbx+8]
    mov rsi, [rbx+0]
    mov rdi, rax
    test rcx, rcx
    jz .lp_cpy_done
.lp_cpy_loop:
    mov rdx, [rsi]
    mov [rdi], rdx
    add rsi, 8
    add rdi, 8
    dec rcx
    jnz .lp_cpy_loop
.lp_cpy_done:
    mov [rbx+0], rax                ;  arr.ptr = new_ptr
    mov [rbx+16], r13               ;  arr.cap = new_cap

.lp_store:
    mov rax, [rbx+0]                ;  ptr
    mov rcx, [rbx+8]                ;  len
    mov [rax+rcx*8], r12            ;  ptr[len] = elem
    inc rcx
    mov [rbx+8], rcx                ;  len++

    add rsp, 8
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  pop (rdi = DynArray*) → rax = последний элемент
;  Если len == 0 — lang_panic и выход с кодом 1.
;  ──────────────────────────────────────────────────────────────
global lang_pop
lang_pop:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 8                      ;  выравнивание

    mov rbx, rdi
    mov rax, [rbx+8]                ;  len
    test rax, rax
    jz .lpo_empty

    dec rax
    mov [rbx+8], rax                ;  len--
    mov rcx, [rbx+0]                ;  ptr
    mov rax, [rcx+rax*8]            ;  rax = ptr[new_len]

    add rsp, 8
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lpo_empty:
    lea rdi, [rel pop_empty_msg]
    call lang_panic
