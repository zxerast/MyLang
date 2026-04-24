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
    push rbx                        ;  rbx — callee-saved по SysV ABI
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
    add rsp, 32
    pop rbx
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
;  print_char (rdi = ASCII-код) — печатает один байт (для типа char)
;  ──────────────────────────────────────────────────────────────
global print_char
print_char:
    push rbp
    mov rbp, rsp
    sub rsp, 16                     ;  выравнивание + 1 байт под символ
    mov [rsp], dil
    mov rax, 1                      ;  sys_write
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  print_float (xmm0 = double) — печатает float формата "[-]int.frac", 6 цифр после точки
;  ──────────────────────────────────────────────────────────────
extern print_int
global print_float
print_float:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 24                        ;  [rsp..rsp+0] — char, [rsp+8..rsp+15] — xmm0 save, [rsp+16..rsp+21] — frac buf

    ;  Знак: печатаем '-', затем берём abs(xmm0)
    movq rax, xmm0
    test rax, rax
    jns .pf_pos
    mov byte [rsp], '-'
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    movq rax, xmm0
    mov rbx, 0x7FFFFFFFFFFFFFFF
    and rax, rbx
    movq xmm0, rax
.pf_pos:
    ;  Целая часть: сохраняем xmm0, печатаем int
    movsd [rsp+8], xmm0
    cvttsd2si rax, xmm0
    mov rdi, rax
    call print_int
    movsd xmm0, [rsp+8]
    ;  Точка
    mov byte [rsp], '.'
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    mov rdx, 1
    syscall
    ;  Дробная: xmm0 - trunc(xmm0), затем * 1000000
    cvttsd2si rax, xmm0
    cvtsi2sd xmm1, rax
    subsd xmm0, xmm1
    mov rax, 1000000
    cvtsi2sd xmm1, rax
    mulsd xmm0, xmm1
    cvttsd2si rax, xmm0                ;  rax = 0..999999
    ;  Раскладываем 6 цифр с ведущими нулями (справа налево)
    lea rdi, [rsp+22]                  ;  конец буфера (за последним байтом)
    mov rcx, 6
    mov rbx, 10
.pf_frac_loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    dec rdi
    mov [rdi], dl
    loop .pf_frac_loop
    mov rsi, rdi
    mov rdx, 6
    mov rax, 1
    mov rdi, 1
    syscall

    add rsp, 24
    pop rbx
    pop rbp
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
