#pragma once

#include "Type.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

enum class SymbolKind {
    Variable,
    Function,
    Struct,
    TypeAlias,
    Namespace,
};

struct StructInfo {
    std::string name;   //  Имя структуры
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> fields;  // массив пар: имя поля -> тип
};

struct FuncInfo {
    std::shared_ptr<Type> returnType;
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> params;  // имя параметра -> тип
};

struct Symbol {
    std::string name;   
    SymbolKind kind;    //  Что за этим именем стоит
    std::shared_ptr<Type> type;       // тип переменной / возвращаемый тип функции

    bool isConst = false;
    bool isExported = false;
    bool isInitialized = false;

    // Доп. информация в зависимости от kind
    std::shared_ptr<FuncInfo> funcInfo = nullptr;
    std::shared_ptr<StructInfo> structInfo = nullptr;
};

struct Scope {
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;   //  Все символы внутри области видимости
    std::shared_ptr<Scope> parent = nullptr;    //  Ссылка на внешнюю область
};

// Объявления функций семантического анализатора (реализация в Semantic.cpp)

struct Program;
struct Stmt;
struct Expr;
struct Block;

void analyzeStmt(Stmt* stmt);      // Анализ одной инструкции
void analyzeExpr(Expr* expr);      // Анализ одного выражения
void analyzeBlock(Block* block);   // Анализ блока (вход/выход из scope)
std::expected<void, std::string> analyze(Program* program);  // Точка входа
