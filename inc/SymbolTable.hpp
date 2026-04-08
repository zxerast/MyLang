#pragma once

#include "Type.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <expected>

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

class SymbolTable {
    std::shared_ptr<Scope> current;     //      Указатель на текущую область видимости

public:
    SymbolTable() : current(std::make_shared<Scope>()) {}

    void enterScope() {
        auto inner = std::make_shared<Scope>();     //  Вошли в новую
        inner->parent = current;    //  Ставим текущую как родителя
        current = inner;
    }

    void exitScope() {
        if (current->parent)
            current = current->parent;
    }

    std::expected<void, std::string> declare(std::shared_ptr<Symbol> sym) {     // Объявляем символ в текущем scope
        if (current->symbols.contains(sym->name))   // Возвращает ошибку если имя уже занято в этом же scope
            return std::unexpected("'" + sym->name + "' is already declared in this scope");

        current->symbols[sym->name] = sym;
        return {};
    }

    std::shared_ptr<Symbol> resolve(const std::string& name) {
        for (auto scope = current; scope != nullptr; scope = scope->parent) {
            auto it = scope->symbols.find(name);    // Найти символ по имени, поднимаясь по цепочке scope
            if (it != scope->symbols.end())
                return it->second;
        }
        return nullptr;     // Если не найдём
    }
};

// Семантический анализатор (реализация в Semantic.cpp)

struct Program;
struct Stmt;
struct Expr;
struct Block;

class SemanticAnalyzer {
    SymbolTable table;
    std::vector<std::string> errors;
    std::shared_ptr<Type> currentReturnType;  //  Тип возврата текущей функции (для проверки return)

    void registerBuiltins();                                    //  Регистрация print, len, input, exit, panic
    void collectTopLevel(const std::vector<Stmt*>& decls);      //  Первый проход — собираем имена top-level объявлений
    std::shared_ptr<Type> resolveTypeName(const std::string& name);  //  Преобразование строки типа в Type

    std::shared_ptr<Type> analyzeExpr(Expr* expr);  // Анализ выражения, возвращает его тип
    void analyzeStmt(Stmt* stmt);                   // Анализ одной инструкции
    void analyzeBlock(Block* block);                // Анализ блока (вход/выход из scope)

public:
    std::expected<void, std::string> analyze(Program* program);  // Точка входа
};
