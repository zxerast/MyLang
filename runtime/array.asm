;  MyLang runtime — динамические массивы: lang_push, lang_pop
;  DynArray layout (24 байта): [arr+0]=ptr, [arr+8]=len, [arr+16]=cap

section .rodata
pop_empty_msg: db "pop from empty array", 0

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

;  push_sized (rdi = DynArray*, rsi = src address, rdx = element size)
;  Для composite-элементов: буфер растёт в байтах elemSize * cap,
;  затем копируется elemSize байт из src в ptr[len].
global lang_push_sized
lang_push_sized:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    mov rbx, rdi                    ;  arr*
    mov r12, rsi                    ;  src
    mov r13, rdx                    ;  elemSize

    mov rax, [rbx+8]                ;  len
    cmp rax, [rbx+16]               ;  len == cap?
    jne .lps_store

    mov r14, [rbx+16]
    test r14, r14
    jnz .lps_double
    mov r14, 8
    jmp .lps_alloc
.lps_double:
    shl r14, 1

.lps_alloc:
    mov rdi, r14
    imul rdi, r13
    call lang_alloc

    mov rcx, [rbx+8]
    imul rcx, r13                   ;  bytes to copy
    mov rsi, [rbx+0]
    mov rdi, rax
    test rcx, rcx
    jz .lps_cpy_done
.lps_cpy_loop:
    mov dl, [rsi]
    mov [rdi], dl
    inc rsi
    inc rdi
    dec rcx
    jnz .lps_cpy_loop
.lps_cpy_done:
    mov [rbx+0], rax
    mov [rbx+16], r14

.lps_store:
    mov rdi, [rbx+0]
    mov r15, [rbx+8]
    imul r15, r13
    add rdi, r15
    mov rsi, r12
    mov rcx, r13
    test rcx, rcx
    jz .lps_store_done
.lps_store_loop:
    mov dl, [rsi]
    mov [rdi], dl
    inc rsi
    inc rdi
    dec rcx
    jnz .lps_store_loop
.lps_store_done:
    mov rax, [rbx+8]
    inc rax
    mov [rbx+8], rax

    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  pop (rdi = DynArray*, rsi = номер строки) → rax = последний элемент
;  Если len == 0 — lang_panic(pop_empty_msg, rsi) и выход с кодом 1.
;  ──────────────────────────────────────────────────────────────
global lang_pop
lang_pop:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    sub rsp, 8                      ;  выравнивание (2 push + sub 8 = 24 → rsp%16==0)

    mov rbx, rdi
    mov r12, rsi                    ;  сохраняем номер строки для возможного panic
    mov rax, [rbx+8]                ;  len
    test rax, rax
    jz .lpo_empty

    dec rax
    mov [rbx+8], rax                ;  len--
    mov rcx, [rbx+0]                ;  ptr
    mov rax, [rcx+rax*8]            ;  rax = ptr[new_len]

    add rsp, 8
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lpo_empty:
    lea rdi, [rel pop_empty_msg]
    mov rsi, r12
    call lang_panic

;  pop_sized (rdi = DynArray*, rsi = line, rdx = element size) → rax = address
global lang_pop_sized
lang_pop_sized:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    sub rsp, 8

    mov rbx, rdi
    mov r12, rsi
    mov r13, rdx
    mov rax, [rbx+8]
    test rax, rax
    jz .lpos_empty

    dec rax
    mov [rbx+8], rax
    imul rax, r13
    add rax, [rbx+0]

    add rsp, 8
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lpos_empty:
    lea rdi, [rel pop_empty_msg]
    mov rsi, r12
    call lang_panic

section .note.GNU-stack noalloc noexec nowrite progbits
