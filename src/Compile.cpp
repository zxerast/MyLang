#include "Ast.hpp"
#include <fstream>
#include <set>
#include <string>
#include <expected>

// Собирает все имена переменных (Identifier) из дерева
void collectVars(Expr *head, std::set<std::string>& vars) {
    if (head == nullptr) return;

    if (auto *id = dynamic_cast<Identifier*>(head)) {
        vars.insert(id->name);
    }

    if (auto *bin = dynamic_cast<Binary*>(head)) {
        collectVars(bin->left, vars);
        collectVars(bin->right, vars);
    }

    if (auto *un = dynamic_cast<Unary*>(head)) {
        collectVars(un->operand, vars);
    }
}

std::expected<void, std::string> compile(Expr *head, std::ofstream &file){
    if (auto *num = dynamic_cast<Number*>(head)){
        file << "mov rax, " << static_cast<long long>(num->value) << "\n";
        file << "push rax\n";
        return {};
    }

    // Iden в выражении — загружаем значение переменной
    if (auto *id = dynamic_cast<Identifier*>(head)){
        file << "mov rax, [" << id->name << "]\n";
        file << "push rax\n";
        return {};
    }

    // Унарный минус
    if (auto *un = dynamic_cast<Unary*>(head)){
        if (un->op == Operand::UnaryMinus){
            auto res = compile(un->operand, file);
            if (!res) return res;
            file << "pop rax\n";
            file << "neg rax\n";
            file << "push rax\n";
            return {};
        }
    }

    if (auto *bin = dynamic_cast<Binary*>(head)){
        // Присваивание: левый узел — Identifier, правый — выражение
        if (bin->op == Operand::Equal){
            auto res = compile(bin->right, file);
            if (!res) return res;
            file << "pop rax\n";
            auto *id = dynamic_cast<Identifier*>(bin->left);
            file << "mov [" << id->name << "], rax\n";
            file << "push rax\n";  // присваивание возвращает значение
            return {};
        }

        auto resL = compile(bin->left, file);
        if (!resL) return resL;
        auto resR = compile(bin->right, file);
        if (!resR) return resR;

        file << "pop rbx\n";
        file << "pop rax\n";

        switch (bin->op){
            case Operand::Add:
                file << "add rax, rbx\n";
                break;

            case Operand::Sub:
                file << "sub rax, rbx\n";
                break;

            case Operand::Mul:
                file << "imul rax, rbx\n";
                break;

            case Operand::Div:
                file << "cqo\n";
                file << "idiv rbx\n";
                break;

            default:
                return std::unexpected("Ошибка компиляции: неподдерживаемый бинарный оператор");
        }
        file << "push rax\n";
        return {};
    }
    return {};
}

std::expected<void, std::string> generate(const std::vector<Expr*>& nodes, std::ofstream &file){
    std::set<std::string> vars;
    for (Expr* node : nodes) {
        collectVars(node, vars);
    }

    if (!vars.empty()) {
        file << "section .bss\n";
        for (const std::string& name : vars) {
            file << name << ": resq 1\n";
        }
    }

    file << "global _start\n";
    file << "section .text\n";
    file << "_start:\n";

    for (Expr* node : nodes) {
        auto res = compile(node, file);
        if (!res) return res;
        if (node != nodes.back()) {
            file << "pop rax\n";
        }
    }

    file << "pop rdi\n";
    file << "mov rax, 60\n";
    file << "syscall\n";

    file.close();

    system("nasm -f elf64 a.asm -o a.o");
    system("ld a.o -o a");
    system("rm a.o");
    return {};
}
