;  MyLang runtime — ввод/вывод: print_int, print_string, print_bool, print_space, print_newline, lang_input

section .rodata
s_true:     db "true"
s_false:    db "false"
nl:         db 10
space_byte: db ' '
input_invalid_int:   db "invalid integer input", 0
input_invalid_float: db "invalid float input", 0
input_invalid_char:  db "char input overflow", 0
input_array_count:   db "input element count mismatch", 0

section .text

extern lang_alloc
extern lang_panic

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
    mov rbx, rax                    ;  сохраним указатель на буфер

    xor r12, r12                    ;  length

.inp_read_loop:
    cmp r12, 4095
    jae .inp_term
    xor rdi, rdi                    ;  fd = 0 (stdin)
    lea rsi, [rbx + r12]
    mov rdx, 1
    xor rax, rax                    ;  sys_read
    syscall
    test rax, rax
    jle .inp_term
    cmp byte [rbx + r12], 10
    je .inp_term
    inc r12
    jmp .inp_read_loop

.inp_term:
    mov byte [rbx + r12], 0
    jmp .inp_done
.inp_empty:
    mov byte [rbx], 0

.inp_done:
    mov rax, rbx                    ;  результат — указатель на буфер
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

;  parse_next_int(rdi = char*, rsi = line)
;  rax = int64 value, rdi = new cursor, rcx = 1 if found; rcx = 0 on end.
__lang_parse_next_int:
.pni_skip:
    mov dl, [rdi]
    cmp dl, ' '
    je .pni_skip_advance
    cmp dl, 9
    je .pni_skip_advance
    cmp dl, 0
    je .pni_eof

    xor r8, r8                      ;  negative flag
    cmp dl, '-'
    je .pni_negative
    cmp dl, '+'
    je .pni_positive
    jmp .pni_digits_start

.pni_skip_advance:
    inc rdi
    jmp .pni_skip

.pni_negative:
    mov r8, 1
    inc rdi
    jmp .pni_digits_start

.pni_positive:
    inc rdi

.pni_digits_start:
    xor rax, rax
    xor r10, r10                    ;  digit count

.pni_digit_loop:
    mov dl, [rdi]
    cmp dl, '0'
    jb .pni_digit_end
    cmp dl, '9'
    ja .pni_digit_end
    imul rax, 10
    movzx r11, dl
    sub r11, '0'
    add rax, r11
    inc r10
    inc rdi
    jmp .pni_digit_loop

.pni_digit_end:
    test r10, r10
    jz .pni_invalid
    cmp dl, 0
    je .pni_found
    cmp dl, ' '
    je .pni_found
    cmp dl, 9
    je .pni_found
    jmp .pni_invalid

.pni_found:
    test r8, r8
    jz .pni_positive_result
    neg rax
.pni_positive_result:
    mov rcx, 1
    ret

.pni_eof:
    xor rcx, rcx
    ret

.pni_invalid:
    sub rsp, 8
    lea rdi, [rel input_invalid_int]
    call lang_panic

global lang_parse_input_int
lang_parse_input_int:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 8

    call __lang_parse_next_int
    test rcx, rcx
    jz .lpi_invalid
    mov rbx, rax

.lpi_tail:
    mov dl, [rdi]
    cmp dl, ' '
    je .lpi_tail_advance
    cmp dl, 9
    je .lpi_tail_advance
    cmp dl, 0
    jne .lpi_invalid

    mov rax, rbx
    add rsp, 8
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lpi_tail_advance:
    inc rdi
    jmp .lpi_tail

.lpi_invalid:
    lea rdi, [rel input_invalid_int]
    call lang_panic

global lang_parse_input_char
lang_parse_input_char:
    push rbp
    mov rbp, rsp

    mov al, [rdi]
    cmp al, 0
    je .lpc_invalid
    cmp byte [rdi + 1], 0
    jne .lpc_invalid

    movzx rax, al
    mov rsp, rbp
    pop rbp
    ret

.lpc_invalid:
    lea rdi, [rel input_invalid_char]
    call lang_panic

;  parse_next_float(rdi = char*, rsi = line)
;  rax = float64 bits, rdi = new cursor, rcx = 1 if found; rcx = 0 on end.
__lang_parse_next_float:
.pnf_skip:
    mov dl, [rdi]
    cmp dl, ' '
    je .pnf_skip_advance
    cmp dl, 9
    je .pnf_skip_advance
    cmp dl, 0
    je .pnf_eof

    xor r8, r8                      ;  negative flag
    cmp dl, '-'
    je .pnf_negative
    cmp dl, '+'
    je .pnf_positive
    jmp .pnf_digits_start

.pnf_skip_advance:
    inc rdi
    jmp .pnf_skip

.pnf_negative:
    mov r8, 1
    inc rdi
    jmp .pnf_digits_start

.pnf_positive:
    inc rdi

.pnf_digits_start:
    xor rax, rax                    ;  integer accumulator
    xor r9, r9                      ;  total digit count
    xor r10, r10                    ;  fraction accumulator
    mov r11, 1                      ;  fraction divisor

.pnf_int_loop:
    mov dl, [rdi]
    cmp dl, '0'
    jb .pnf_maybe_dot
    cmp dl, '9'
    ja .pnf_maybe_dot
    imul rax, 10
    movzx rcx, dl
    sub rcx, '0'
    add rax, rcx
    inc r9
    inc rdi
    jmp .pnf_int_loop

.pnf_maybe_dot:
    cmp dl, '.'
    jne .pnf_digit_end
    inc rdi

.pnf_frac_loop:
    mov dl, [rdi]
    cmp dl, '0'
    jb .pnf_digit_end
    cmp dl, '9'
    ja .pnf_digit_end
    imul r10, 10
    movzx rcx, dl
    sub rcx, '0'
    add r10, rcx
    imul r11, 10
    inc r9
    inc rdi
    jmp .pnf_frac_loop

.pnf_digit_end:
    test r9, r9
    jz .pnf_invalid
    cmp dl, 0
    je .pnf_convert
    cmp dl, ' '
    je .pnf_convert
    cmp dl, 9
    je .pnf_convert
    jmp .pnf_invalid

.pnf_convert:
    cvtsi2sd xmm0, rax
    cmp r11, 1
    je .pnf_apply_sign
    cvtsi2sd xmm1, r10
    cvtsi2sd xmm2, r11
    divsd xmm1, xmm2
    addsd xmm0, xmm1

.pnf_apply_sign:
    test r8, r8
    jz .pnf_done
    pxor xmm3, xmm3
    subsd xmm3, xmm0
    movapd xmm0, xmm3

.pnf_done:
    movq rax, xmm0
    mov rcx, 1
    ret

.pnf_eof:
    xor rcx, rcx
    ret

.pnf_invalid:
    sub rsp, 8
    lea rdi, [rel input_invalid_float]
    call lang_panic

global lang_parse_input_float
lang_parse_input_float:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 8

    call __lang_parse_next_float
    test rcx, rcx
    jz .lpfl_invalid
    mov rbx, rax

.lpfl_tail:
    mov dl, [rdi]
    cmp dl, ' '
    je .lpfl_tail_advance
    cmp dl, 9
    je .lpfl_tail_advance
    cmp dl, 0
    jne .lpfl_invalid

    mov rax, rbx
    add rsp, 8
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lpfl_tail_advance:
    inc rdi
    jmp .lpfl_tail

.lpfl_invalid:
    lea rdi, [rel input_invalid_float]
    call lang_panic

__lang_parse_next_string:
.pns_skip:
    mov dl, [rdi]
    cmp dl, ' '
    je .pns_skip_advance
    cmp dl, 9
    je .pns_skip_advance
    cmp dl, 0
    je .pns_eof

    mov rax, rdi
.pns_loop:
    mov dl, [rdi]
    cmp dl, 0
    je .pns_found
    cmp dl, ' '
    je .pns_terminate
    cmp dl, 9
    je .pns_terminate
    inc rdi
    jmp .pns_loop

.pns_skip_advance:
    inc rdi
    jmp .pns_skip

.pns_terminate:
    mov byte [rdi], 0
    inc rdi

.pns_found:
    mov rcx, 1
    ret

.pns_eof:
    xor rcx, rcx
    ret

__lang_parse_next_char:
.pnc_skip:
    mov dl, [rdi]
    cmp dl, ' '
    je .pnc_skip_advance
    cmp dl, 9
    je .pnc_skip_advance
    cmp dl, 0
    je .pnc_eof

    movzx rax, dl
    inc rdi
    mov dl, [rdi]
    cmp dl, 0
    je .pnc_found
    cmp dl, ' '
    je .pnc_consume_sep
    cmp dl, 9
    je .pnc_consume_sep
    jmp .pnc_invalid

.pnc_skip_advance:
    inc rdi
    jmp .pnc_skip

.pnc_consume_sep:
    inc rdi

.pnc_found:
    mov rcx, 1
    ret

.pnc_eof:
    xor rcx, rcx
    ret

.pnc_invalid:
    sub rsp, 8
    lea rdi, [rel input_invalid_char]
    call lang_panic

__lang_input_elem_size_fixed:
    cmp rdi, 1
    je .lies_1
    cmp rdi, 5
    je .lies_1
    cmp rdi, 12
    je .lies_1
    cmp rdi, 2
    je .lies_2
    cmp rdi, 6
    je .lies_2
    cmp rdi, 3
    je .lies_4
    cmp rdi, 7
    je .lies_4
    cmp rdi, 9
    je .lies_4
    mov rax, 8
    ret
.lies_1:
    mov rax, 1
    ret
.lies_2:
    mov rax, 2
    ret
.lies_4:
    mov rax, 4
    ret

__lang_input_elem_size_dyn:
    cmp rdi, 3                      ;  int32[] follows existing qword DynArray layout
    je .lied_8
    jmp __lang_input_elem_size_fixed
.lied_8:
    mov rax, 8
    ret

__lang_parse_token_by_type:
    cmp rdx, 9
    je .lpt_float32
    cmp rdx, 10
    je .lpt_float64
    cmp rdx, 11
    je .lpt_string
    cmp rdx, 12
    je .lpt_char
    jmp __lang_parse_next_int

.lpt_float32:
    call __lang_parse_next_float
    test rcx, rcx
    jz .lpt_done
    movq xmm0, rax
    cvtsd2ss xmm0, xmm0
    movd eax, xmm0
    ret

.lpt_float64:
    jmp __lang_parse_next_float

.lpt_string:
    jmp __lang_parse_next_string

.lpt_char:
    jmp __lang_parse_next_char

.lpt_done:
    ret

global lang_input_array_fixed
lang_input_array_fixed:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 24

    mov r12, rdi                    ;  type code
    mov r13, rsi                    ;  expected count
    mov r14, rdx                    ;  source line

    call lang_input
    mov r15, rax                    ;  cursor

    mov rdi, r12
    call __lang_input_elem_size_fixed
    mov [rbp - 8], rax              ;  elem size

    mov rax, r13
    imul rax, [rbp - 8]
    mov rdi, rax
    call lang_alloc
    mov rbx, rax                    ;  result buffer
    mov qword [rbp - 16], 0         ;  parsed count

.lia_loop:
    mov rax, [rbp - 16]
    cmp rax, r13
    je .lia_after_expected

    mov rdi, r15
    mov rsi, r14
    mov rdx, r12
    call __lang_parse_token_by_type
    test rcx, rcx
    jz .lia_count_error

    mov r15, rdi
    mov r10, [rbp - 16]
    imul r10, [rbp - 8]
    lea r11, [rbx + r10]
    mov r10, [rbp - 8]
    cmp r10, 1
    je .lia_store_1
    cmp r10, 2
    je .lia_store_2
    cmp r10, 4
    je .lia_store_4
    mov [r11], rax
    jmp .lia_stored
.lia_store_1:
    mov [r11], al
    jmp .lia_stored
.lia_store_2:
    mov [r11], ax
    jmp .lia_stored
.lia_store_4:
    mov [r11], eax
.lia_stored:
    inc qword [rbp - 16]
    jmp .lia_loop

.lia_after_expected:
    mov dl, [r15]
    cmp dl, ' '
    je .lia_tail_advance
    cmp dl, 9
    je .lia_tail_advance
    cmp dl, 0
    jne .lia_count_error

    mov rax, rbx
    add rsp, 24
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.lia_tail_advance:
    inc r15
    jmp .lia_after_expected

.lia_count_error:
    lea rdi, [rel input_array_count]
    mov rsi, r14
    call lang_panic

global lang_input_array_dyn
lang_input_array_dyn:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 24

    mov r12, rdi                    ;  type code
    mov r14, rsi                    ;  source line

    call lang_input
    mov r15, rax                    ;  cursor

    mov rdi, r12
    call __lang_input_elem_size_dyn
    mov [rbp - 8], rax              ;  elem size

    mov rdi, 24
    call lang_alloc
    mov rbx, rax                    ;  DynArray header

    mov rdi, [rbp - 8]
    imul rdi, 4096
    call lang_alloc
    mov r13, rax                    ;  data buffer

    mov [rbx], r13
    mov qword [rbx + 8], 0
    mov qword [rbx + 16], 4096
    mov qword [rbp - 16], 0         ;  len

.liadg_loop:
    mov rdi, r15
    mov rsi, r14
    mov rdx, r12
    call __lang_parse_token_by_type
    test rcx, rcx
    jz .liadg_done

    mov r15, rdi
    mov r10, [rbp - 16]
    imul r10, [rbp - 8]
    lea r11, [r13 + r10]
    mov r10, [rbp - 8]
    cmp r10, 1
    je .liadg_store_1
    cmp r10, 2
    je .liadg_store_2
    cmp r10, 4
    je .liadg_store_4
    mov [r11], rax
    jmp .liadg_stored
.liadg_store_1:
    mov [r11], al
    jmp .liadg_stored
.liadg_store_2:
    mov [r11], ax
    jmp .liadg_stored
.liadg_store_4:
    mov [r11], eax
.liadg_stored:
    inc qword [rbp - 16]
    jmp .liadg_loop

.liadg_done:
    mov rax, [rbp - 16]
    mov [rbx + 8], rax
    mov rax, rbx
    add rsp, 24
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

global lang_input_int_array_fixed
lang_input_int_array_fixed:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    mov r12, rdi                    ;  expected count
    mov r13, rsi                    ;  source line
    call lang_input
    mov rbx, rax                    ;  cursor

    mov rdi, r12
    shl rdi, 2                      ;  int32[count]
    call lang_alloc
    mov r14, rax                    ;  result buffer
    xor r15, r15                    ;  parsed count

.liaf_loop:
    cmp r15, r12
    je .liaf_after_expected
    mov rdi, rbx
    mov rsi, r13
    call __lang_parse_next_int
    test rcx, rcx
    jz .liaf_count_error
    mov rbx, rdi
    mov [r14 + r15*4], eax
    inc r15
    jmp .liaf_loop

.liaf_after_expected:
    mov dl, [rbx]
    cmp dl, ' '
    je .liaf_tail_advance
    cmp dl, 9
    je .liaf_tail_advance
    cmp dl, 0
    jne .liaf_count_error

    mov rax, r14
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

.liaf_tail_advance:
    inc rbx
    jmp .liaf_after_expected

.liaf_count_error:
    lea rdi, [rel input_array_count]
    mov rsi, r13
    call lang_panic

global lang_input_int_array_dyn
lang_input_int_array_dyn:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8

    mov r12, rdi                    ;  source line
    call lang_input
    mov rbx, rax                    ;  cursor

    mov rdi, 24
    call lang_alloc
    mov r14, rax                    ;  DynArray header

    mov rdi, 32768                  ;  4096 qword slots for int[] input
    call lang_alloc
    mov r13, rax                    ;  int32 buffer

    mov [r14], r13
    mov qword [r14 + 8], 0
    mov qword [r14 + 16], 4096
    xor r15, r15                    ;  len

.liad_loop:
    mov rdi, rbx
    mov rsi, r12
    call __lang_parse_next_int
    test rcx, rcx
    jz .liad_done
    mov rbx, rdi
    mov [r13 + r15*8], rax
    inc r15
    jmp .liad_loop

.liad_done:
    mov [r14 + 8], r15
    mov rax, r14
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    mov rsp, rbp
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
