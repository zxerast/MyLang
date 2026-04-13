#include "CodeGen.hpp"
#include <cstdlib>
#include <fstream>

//  ─── Базовые helper'ы вывода ───

void CodeGen::emit(const std::string& line) {
    text << "    " << line << "\n";
}

void CodeGen::emitLabel(const std::string& label) {
    text << label << ":\n";
}

std::string CodeGen::newLabel(const std::string& hint) {
    return "." + hint + std::to_string(labelCounter++);
}

std::string CodeGen::internString(const std::string& s) {
    auto it = stringPool.find(s);
    if (it != stringPool.end()) return it->second;

    std::string label = "str" + std::to_string(stringCounter++);
    stringPool[s] = label;

    //  Побайтовая запись — NASM-формат `db b1, b2, ..., 0`
    rodata << label << ": db ";
    for (unsigned char c : s)
        rodata << (int)c << ",";
    rodata << "0\n";
    return label;
}

//  ─── Размер типа ───

int CodeGen::sizeOfType(const std::shared_ptr<Type>& t) {
    if (!t) return 8;
    switch (t->kind) {
        case TypeKind::Int8:  case TypeKind::Uint8:  case TypeKind::Bool:  case TypeKind::Char:  return 1;
        case TypeKind::Int16: case TypeKind::Uint16:                                             return 2;
        case TypeKind::Int32: case TypeKind::Uint32: case TypeKind::Float32:                     return 4;
        case TypeKind::Int64: case TypeKind::Uint64: case TypeKind::Float64:                     return 8;
        case TypeKind::String:                                                                   return 8;  //  указатель
        case TypeKind::Array: {
            int elem = sizeOfType(t->elementType);
            return elem * (t->arraySize > 0 ? t->arraySize : 0);
        }
        case TypeKind::DynArray:                                                                 return 24; //  ptr + len + cap
        case TypeKind::Struct: case TypeKind::Class: case TypeKind::Alias:                       return 8;  //  placeholder (посчитаем в Фазе 13)
        case TypeKind::Void:                                                                     return 0;
    }
    return 8;
}

//  ─── Локальные переменные ───

int CodeGen::allocLocal(const std::string& name, const std::shared_ptr<Type>& type) {
    int size = sizeOfType(type);
    if (size < 8) size = 8;                     //  Выравниваем каждую локалку до 8 байт
    currentFrameSize += size;
    int offset = -currentFrameSize;             //  [rbp - currentFrameSize]
    locals[name] = LocalVar{offset, type};
    return offset;
}

const LocalVar* CodeGen::findLocal(const std::string& name) const {
    auto it = locals.find(name);
    return it == locals.end() ? nullptr : &it->second;
}

//  ─── Точка входа ───

std::expected<void, std::string> CodeGen::generate(Program* program, const std::string& outPath) {
    //  Пролог точки входа: _start → call lang_main → sys_exit(rax)
    text << "global _start\n";
    text << "_start:\n";
    text << "    call lang_main\n";
    text << "    mov rdi, rax\n";
    text << "    mov rax, 60\n";
    text << "    syscall\n\n";

    //  Первый проход — для каждой функции эмитим пока что пустую заглушку
    //  (Фаза 2 заменит это на реальную компиляцию тела)
    bool hasMain = false;
    for (auto* decl : program->decls) {
        FuncDecl* fn = nullptr;
        if (auto* f = dynamic_cast<FuncDecl*>(decl)) fn = f;
        else if (auto* exp = dynamic_cast<ExportDecl*>(decl))
            fn = dynamic_cast<FuncDecl*>(exp->decl);
        if (!fn) continue;

        std::string symbol = "lang_" + fn->name;
        if (fn->name == "main") hasMain = true;

        text << "global " << symbol << "\n";
        emitLabel(symbol);
        emit("xor rax, rax");   //  пока возвращаем 0
        emit("ret");
        text << "\n";
    }

    if (!hasMain)
        return std::unexpected("codegen: no 'main' function defined");

    return finalize(outPath);
}

//  ─── Финальная сборка ───

std::expected<void, std::string> CodeGen::finalize(const std::string& outPath) {
    std::string asmPath = outPath + ".asm";
    std::string objPath = outPath + ".o";

    //  Склеиваем секции в итоговый файл
    std::ofstream out(asmPath);
    if (!out)
        return std::unexpected("codegen: cannot open '" + asmPath + "' for writing");

    if (!data.str().empty())   out << "section .data\n"   << data.str()   << "\n";
    if (!rodata.str().empty()) out << "section .rodata\n" << rodata.str() << "\n";
    if (!bss.str().empty())    out << "section .bss\n"    << bss.str()    << "\n";
    out << "section .text\n" << text.str();
    out.close();

    //  nasm -f elf64
    std::string nasmCmd = "nasm -f elf64 " + asmPath + " -o " + objPath;
    if (std::system(nasmCmd.c_str()) != 0)
        return std::unexpected("codegen: nasm failed on '" + asmPath + "'");

    //  ld — пока без рантайма, он появится в Фазе 8
    std::string ldCmd = "ld " + objPath + " -o " + outPath;
    if (std::system(ldCmd.c_str()) != 0)
        return std::unexpected("codegen: ld failed");

    //  Чистим промежуточный объектник
    std::remove(objPath.c_str());
    return {};
}
