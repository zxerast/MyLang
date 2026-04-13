#pragma once

#include "Ast.hpp"
#include "Type.hpp"
#include <expected>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

//  Информация об одной локальной переменной в текущей функции
struct LocalVar {
    int offset;                     //  Отрицательное смещение от rbp
    std::shared_ptr<Type> type;
};

//  Пара меток для break/continue внутри цикла
struct LoopLabels {
    std::string breakLabel;
    std::string continueLabel;
};

class CodeGen {
    //  Буферы секций — собираем в них инструкции, склеиваем в finalize()
    std::ostringstream data;        //  .data    — инициализированные глобальные
    std::ostringstream rodata;      //  .rodata  — строковые/float литералы
    std::ostringstream bss;         //  .bss     — неинициализированные глобальные
    std::ostringstream text;        //  .text    — код

    int labelCounter = 0;           //  Счётчик для уникальных меток .L0, .L1, ...
    int stringCounter = 0;          //  Счётчик строковых литералов .str0, .str1, ...

    //  Текущая функция
    std::unordered_map<std::string, LocalVar> locals;
    int currentFrameSize = 0;       //  Сколько байт выделено в текущем фрейме
    std::string currentEpilogLabel; //  Метка эпилога для return
    std::vector<LoopLabels> loopStack;

    //  Пул строковых литералов: текст → метка
    std::unordered_map<std::string, std::string> stringPool;

    //  ─── Helper'ы ───
    void emit(const std::string& line);         //  Пишет строку в .text с отступом
    void emitLabel(const std::string& label);   //  Пишет метку без отступа
    std::string newLabel(const std::string& hint = "L");    //  Генерирует уникальную метку
    std::string internString(const std::string& s);         //  Добавляет строку в пул, возвращает метку

    //  Размер типа в байтах (с учётом выравнивания)
    static int sizeOfType(const std::shared_ptr<Type>& t);

    //  Выделение локальной переменной в текущем фрейме
    int allocLocal(const std::string& name, const std::shared_ptr<Type>& type);
    const LocalVar* findLocal(const std::string& name) const;

    //  Финальная сборка: склеивает секции, пишет .asm, вызывает nasm + ld
    std::expected<void, std::string> finalize(const std::string& outPath);

public:
    //  Точка входа — принимает AST и путь выходного исполняемого файла
    std::expected<void, std::string> generate(Program* program, const std::string& outPath);
};
