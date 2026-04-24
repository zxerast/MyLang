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
    push r12

    mov rdi, 4096
    call lang_alloc
    mov rbx, rax                    ;  буфер
    xor r12, r12                    ;  длина прочитанного
.inp_read_byte:
    cmp r12, 4095
    jae .inp_term
    xor rax, rax                    ;  sys_read
    xor rdi, rdi                    ;  fd = 0
    lea rsi, [rbx + r12]
    mov rdx, 1                      ;  по одному байту, чтобы остановиться на \n и не поглотить следующую строку
    syscall
    test rax, rax
    jle .inp_term                   ;  EOF или ошибка
    mov dl, [rbx + r12]
    inc r12
    cmp dl, 10
    je .inp_strip_nl
    jmp .inp_read_byte
.inp_strip_nl:
    dec r12                         ;  не включаем \n в строку
.inp_term:
    mov byte [rbx + r12], 0

    mov rax, rbx
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  lang_input_int () → rax = int64 — читает строку через lang_input
;  и парсит как знаковое десятичное. Мусорные символы игнорируются
;  после первого не-цифрового байта (после знака). Пусто/мусор → 0.
;  ──────────────────────────────────────────────────────────────
global lang_input_int
lang_input_int:
    push rbp
    mov rbp, rsp
    sub rsp, 16                     ;  выравнивание + слот

    call lang_input                 ;  rax = char*
    mov rcx, rax                    ;  rcx — курсор
    xor rax, rax                    ;  результат
    xor r8, r8                      ;  знак (1 если минус)

    mov dl, [rcx]
    cmp dl, '-'
    jne .lii_checkplus
    mov r8, 1
    inc rcx
    jmp .lii_loop
.lii_checkplus:
    cmp dl, '+'
    jne .lii_loop
    inc rcx
.lii_loop:
    mov dl, [rcx]
    test dl, dl
    jz .lii_done
    cmp dl, '0'
    jb .lii_done
    cmp dl, '9'
    ja .lii_done
    imul rax, rax, 10
    sub dl, '0'
    movzx rdx, dl
    add rax, rdx
    inc rcx
    jmp .lii_loop
.lii_done:
    test r8, r8
    jz .lii_ret
    neg rax
.lii_ret:
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  lang_input_float () → rax = бит-паттерн float64
;  Парсит строку [-]?digits(.digits)? ; остальное игнорирует.
;  ──────────────────────────────────────────────────────────────
global lang_input_float
lang_input_float:
    push rbp
    mov rbp, rsp
    sub rsp, 16

    call lang_input
    mov rcx, rax                    ;  курсор
    pxor xmm0, xmm0                 ;  целая часть (double)
    pxor xmm1, xmm1                 ;  дробная часть
    pxor xmm2, xmm2                 ;  делитель для дробной (10, 100, ...)
    xor r8, r8                      ;  знак

    mov dl, [rcx]
    cmp dl, '-'
    jne .lif_checkplus
    mov r8, 1
    inc rcx
    jmp .lif_int
.lif_checkplus:
    cmp dl, '+'
    jne .lif_int
    inc rcx
.lif_int:
    mov dl, [rcx]
    test dl, dl
    jz .lif_done
    cmp dl, '.'
    je .lif_dot
    cmp dl, '0'
    jb .lif_done
    cmp dl, '9'
    ja .lif_done
    ;  xmm0 = xmm0*10 + digit
    mov rax, 10
    cvtsi2sd xmm3, rax
    mulsd xmm0, xmm3
    sub dl, '0'
    movzx rax, dl
    cvtsi2sd xmm3, rax
    addsd xmm0, xmm3
    inc rcx
    jmp .lif_int
.lif_dot:
    inc rcx                          ;  пропускаем '.'
    mov rax, 1
    cvtsi2sd xmm2, rax               ;  divisor = 1
.lif_frac:
    mov dl, [rcx]
    test dl, dl
    jz .lif_done
    cmp dl, '0'
    jb .lif_done
    cmp dl, '9'
    ja .lif_done
    mov rax, 10
    cvtsi2sd xmm3, rax
    mulsd xmm2, xmm3                 ;  divisor *= 10
    sub dl, '0'
    movzx rax, dl
    cvtsi2sd xmm3, rax
    divsd xmm3, xmm2                 ;  digit/divisor
    addsd xmm0, xmm3
    inc rcx
    jmp .lif_frac
.lif_done:
    test r8, r8
    jz .lif_ret
    ;  negate xmm0 via xor на знаковый бит
    mov rax, 0x8000000000000000
    movq xmm3, rax
    xorpd xmm0, xmm3
.lif_ret:
    movq rax, xmm0
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  lang_input_bool () → rax = 0/1. "true"/"1" → 1, иначе 0.
;  ──────────────────────────────────────────────────────────────
global lang_input_bool
lang_input_bool:
    push rbp
    mov rbp, rsp
    sub rsp, 16

    call lang_input
    ;  сравним с "1"
    mov dl, [rax]
    cmp dl, '1'
    jne .lib_checktrue
    mov dl, [rax+1]
    test dl, dl
    jnz .lib_zero
    mov rax, 1
    jmp .lib_done
.lib_checktrue:
    ;  "true" (4 символа + \0)
    cmp byte [rax], 't'
    jne .lib_zero
    cmp byte [rax+1], 'r'
    jne .lib_zero
    cmp byte [rax+2], 'u'
    jne .lib_zero
    cmp byte [rax+3], 'e'
    jne .lib_zero
    mov rax, 1
    jmp .lib_done
.lib_zero:
    xor rax, rax
.lib_done:
    mov rsp, rbp
    pop rbp
    ret

;  ──────────────────────────────────────────────────────────────
;  lang_input_char () → rax = первый байт строки (0 если пусто)
;  ──────────────────────────────────────────────────────────────
global lang_input_char
lang_input_char:
    push rbp
    mov rbp, rsp
    sub rsp, 16

    call lang_input
    movzx rax, byte [rax]
    mov rsp, rbp
    pop rbp
    ret
