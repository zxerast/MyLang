;  MyLang runtime — ввод/вывод: print_int, print_string, print_bool, print_space, print_newline, lang_input

section .rodata
s_true:     db "true"
s_false:    db "false"
nl:         db 10
space_byte: db ' '

section .text

extern lang_alloc

;  ──────────────────────────────────────────────────────────────
;  print_int (rdi = signed int64) — печатает число в stdout (без \n)
;  ──────────────────────────────────────────────────────────────
global print_int
print_int:
    push rbp
    mov rbp, rsp
    sub rsp, 32                     ;  буфер под 32 цифры/знак
    mov rax, rdi
    lea rcx, [rsp+32]               ;  один байт за концом буфера
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
;  print_string (rdi = char*) — печатает нул-терминированную строку (без \n)
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
    ret

;  ──────────────────────────────────────────────────────────────
;  print_bool (rdi = 0/1) — "true" / "false" (без \n)
;  ──────────────────────────────────────────────────────────────
global print_bool
print_bool:
    test rdi, rdi
    jz .pb_false
    mov rax, 1
    mov rdi, 1
    mov rsi, s_true
    mov rdx, 4
    syscall
    ret
.pb_false:
    mov rax, 1
    mov rdi, 1
    mov rsi, s_false
    mov rdx, 5
    syscall
    ret

;  ──────────────────────────────────────────────────────────────
;  print_space — один пробел в stdout
;  ──────────────────────────────────────────────────────────────
global print_space
print_space:
    mov rax, 1
    mov rdi, 1
    mov rsi, space_byte
    mov rdx, 1
    syscall
    ret

;  ──────────────────────────────────────────────────────────────
;  print_newline — перевод строки в stdout
;  ──────────────────────────────────────────────────────────────
global print_newline
print_newline:
    mov rax, 1
    mov rdi, 1
    mov rsi, nl
    mov rdx, 1
    syscall
    ret

;  ──────────────────────────────────────────────────────────────
;  input () → rax = char* — читает строку из stdin (до 4095 байт),
;  срезает завершающий \n, null-терминирует. Буфер выделяется
;  через lang_alloc и остаётся во владении вызывающего.
;  ──────────────────────────────────────────────────────────────
global lang_input
lang_input:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 8                      ;  выравнивание стека до 16 байт перед call

    mov rdi, 4096
    call lang_alloc
    mov rbx, rax                    ;  сохраним указатель на буфер

    ;  sys_read(fd=0, buf=rbx, count=4095) → rax = число прочитанных байт
    xor rdi, rdi                    ;  fd = 0 (stdin)
    mov rsi, rbx
    mov rdx, 4095                   ;  оставляем 1 байт под \0
    xor rax, rax                    ;  sys_read
    syscall

    ;  Если ошибка или EOF — возвращаем пустую строку
    test rax, rax
    jle .inp_empty

    ;  Если последний байт \n — затираем его, иначе ставим \0 после хвоста
    lea rcx, [rbx+rax-1]
    cmp byte [rcx], 10
    jne .inp_term
    mov byte [rcx], 0
    jmp .inp_done
.inp_term:
    mov byte [rbx+rax], 0
    jmp .inp_done
.inp_empty:
    mov byte [rbx], 0

.inp_done:
    mov rax, rbx                    ;  результат — указатель на буфер
    add rsp, 8
    pop rbx
    mov rsp, rbp
    pop rbp
    ret
