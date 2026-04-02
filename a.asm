section .bss
x: resq 1
y: resq 1
global _start
section .text
_start:
mov rax, 4
push rax
mov rax, 9
push rax
pop rbx
pop rax
add rax, rbx
push rax
pop rax
mov [x], rax
push rax
pop rax
mov rax, 2
push rax
pop rax
mov [y], rax
push rax
pop rdi
mov rax, 60
syscall
