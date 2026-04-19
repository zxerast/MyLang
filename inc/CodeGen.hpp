#pragma once

#include "Ast.hpp"
#include "Type.hpp"
#include <expected>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct LocalVar {
    int offset;                     //  Отрицательное смещение от rbp
    std::shared_ptr<Type> type;     //  Тип локальной переменной
};

struct LoopLabels {
    std::string breakLabel;     //  break
    std::string continueLabel;  //  continue
};

class CodeGen {     //  Буферы секций — собираем в них инструкции, склеиваем в finalize()
    std::ostringstream data;        //  инициализированные глобальные переменные
    std::ostringstream rodata;      //  строковые и неизменяемые литералы
    std::ostringstream bss;         //  неинициализированные глобальные переменные
    std::ostringstream text;        //  код

    int labelCounter = 0;           //  Счётчик для уникальных меток .L0, .L1, ...
    int stringCounter = 0;          //  Счётчик строковых литералов .str0, .str1, ...
    int arrayCounter  = 0;          //  Счётчик литералов массивов
    int structCounter = 0;          //  Счётчик временных слотов для структур/классов

    std::unordered_map<std::string, LocalVar> locals;   //  Локальные переменные функции (имя -> тип+смещение)
    int currentFrameSize = 0;       //  Сколько байт выделено в текущем фрейме
    std::string currentEndLabel; //  Метка конца функции для return
    std::vector<LoopLabels> loopStack;  //  Метки break и continue

    std::unordered_map<std::string, std::string> stringPool;    //  Пул строковых литералов: текст → метка

    std::unordered_set<std::string> externCFunctions;   //  Имена C-функций, которые нужно объявить как extern

    std::unordered_map<std::string, std::unordered_map<std::string, int>> structLayouts;    //  Смещение структур/классов: {struct_name -> {field_name -> offset}}. Поле = qword.
    std::unordered_map<std::string, int> structSizes;   //  Суммарный размер типа структуры/класса в байтах
    std::unordered_map<std::string, StructDecl*> structDecls;   //  Имя структуры -> AST-узел (нужен для default-значений полей)
    std::vector<StructDecl*> structDeclsOrdered;                 //  Структуры в порядке объявления — для детерминированной инициализации default-слотов
    std::unordered_map<std::string, ClassDecl*> classDecls;      //  Имя класса -> AST-узел
    std::vector<ClassDecl*> classDeclsOrdered;                    //  Классы в порядке объявления — для детерминированной инициализации
    ClassDecl* currentClass = nullptr;                            //  Класс, чей метод сейчас компилируется (для доступа к self-полям)

    bool hasMain = false;           //  Выставляется compileDecl, когда встречаем функцию main

    std::string newLabel(const std::string& hint = "L");    //  Генерирует уникальную метку
    std::string internString(const std::string& str);       //  Добавляет строку в пул, возвращает метку

    static int sizeOfType(const std::shared_ptr<Type>& type);   //  Размер типа в байтах с учётом выравнивания
    std::shared_ptr<Type> typeFromName(const std::string& name) const;  //  Грубая реконструкция типа из typeName (только DynArray-суффикс T[])

    int allocLocal(const std::string& name, const std::shared_ptr<Type>& type); //  Выделение локальной переменной в текущем фрейме
    const LocalVar* findLocal(const std::string& name) const;

    void collectLayout(const std::string& name, const std::vector<StructField>& fields); //  Запоминает layout полей struct/class (поле = qword)
    void collectDecls(Stmt* decl);                                                       //  Обходит AST и регистрирует layout'ы struct/class
    void compileDecl(Stmt* decl, const std::string& classPrefix);                        //  Компилирует функции/методы; classPrefix добавляется к имени символа

    void compileFunction(FuncDecl* func, bool isMethod = false);   //  Компиляция функции/метода; isMethod=true резервирует self в rdi
    void compileMethod(FuncDecl* f, const std::string& labelName);
    int countLocalsSize(Stmt* size);    //  Первый проход: суммарный размер локалок в теле (8-байтное выравнивание)
    void compileStmt(Stmt* stmt);   //  Компиляция одного стейтмента
    void compileExpr(Expr* expr);   //  Компиляция выражения — результат в rax (для int/ptr)

    std::expected<void, std::string> finalize(const std::string& outPath);  //  Финальная сборка: склеивает секции, пишет .asm, вызывает nasm + ld

public:
    //  Точка входа — принимает AST и путь выходного исполняемого файла
    std::expected<void, std::string> generate(Program* program, const std::string& outPath);
};
