#pragma once

#include "Type.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <expected>

struct Scope;     //  forward declaration для namespace_scope в Symbol
struct FuncInfo;  //  forward declaration для ClassInfo

enum class SymbolKind { //  Что есть наш символ
    Variable,
    Function,
    Struct,
    Class,
    TypeAlias,
    Namespace,
};

struct StructInfo { //  Структура
    std::string name;   //  Имя структуры
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> fields;  // массив пар: имя поля -> тип
};

struct ClassInfo {  //  Класс
    std::string name;   //  Имя класса
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> fields;  //  Поля как в структуре
    std::unordered_map<std::string, std::shared_ptr<FuncInfo>> methods;  //  Методы
    std::shared_ptr<FuncInfo> constructor = nullptr;    //  Указатель на конструктор
    bool hasDestructor = false;
};

struct FuncInfo {   //  Функция
    std::shared_ptr<Type> returnType;   //  Чё возвращает
    std::vector<std::pair<std::string, std::shared_ptr<Type>>> params;  // имя параметра -> тип
    bool isExternC = false;   //  Импортирована из C-заголовка (линкуется через extern)
    bool isVariadic = false;  //  Функция принимает сколько угодно параметров?  
};

struct Symbol { //  Символ кода
    std::string name;   //  Имя
    SymbolKind kind;    //  Что за этим именем стоит
    std::shared_ptr<Type> type;       // тип переменной или возвращаемый тип функции

    bool isConst = false;
    bool isExported = false;
    bool isInitialized = false;

    // Информация в зависимости от контекста, за раз может быть заполнен только один указатель
    std::shared_ptr<FuncInfo> funcInfo = nullptr;    // Данные функции 
    std::shared_ptr<StructInfo> structInfo = nullptr;   // Данные структуры
    std::shared_ptr<ClassInfo> classInfo = nullptr; //  Данные класса
    std::shared_ptr<Scope> namespaceScope = nullptr;  //  Scope пространства имён (для Namespace)
};

struct Scope {
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;   //  Все символы внутри области видимости
    std::shared_ptr<Scope> parent = nullptr;    //  Ссылка на внешнюю область
};

class SymbolTable { //  Таблица символов области видимости
    std::shared_ptr<Scope> current;     //      Указатель на текущую область видимости

public:
    SymbolTable() : current(std::make_shared<Scope>()) {}   //  Сразу же создаём глобальную область видимости

    void enterScope() {
        auto inner = std::make_shared<Scope>();     //  Вошли в новую
        inner->parent = current;    //  Ставим текущую как родителя
        current = inner;    //  А внутреннюю как текущую
    }

    void pushScope(std::shared_ptr<Scope> scope) {
        scope->parent = current;    //  Закидываем новую область видимости в текущую
        current = scope;
    }

    void exitScope() {
        if (current->parent)    //  Возвращаемся на уровень выше, родительскую область видимости
            current = current->parent;
    }

    std::expected<void, std::string> declare(std::shared_ptr<Symbol> sym) {     // Объявляем символ в текущем scope
        if (current->symbols.contains(sym->name)) {  // Возвращает ошибку если имя уже занято в этом же scope
            return std::unexpected("'" + sym->name + "' is already declared in this scope");
        }
        current->symbols[sym->name] = sym;  //  Иначе закидываем в текущий Scope
        return {};
    }

    std::shared_ptr<Scope> currentScope() { 
        return current; 
    }

    std::shared_ptr<Symbol> resolve(const std::string& name) {
        for (auto scope = current; scope != nullptr; scope = scope->parent) {
            auto it = scope->symbols.find(name);    // Найти символ по имени, поднимаясь по цепочке scope
            if (it != scope->symbols.end())
                return it->second;  //  Возвращаем значение по имени символа
        }
        return nullptr;     // Если не найдём
    }
};

// Контекст для обхода AST libclang в processCImport.
// Передаётся через CXClientData, поскольку C-коллбэк не может захватывать переменные.
struct CImportVisitorCtx {
    std::string wantedBase;  //  Basename заголовка — для фильтрации транзитивных включений
    SymbolTable* table;      //  Таблица символов куда регистрируем найденные функции
    int registered = 0;      //  Счётчик успешно зарегистрированных функций
    int skipped = 0;         //  Счётчик пропущенных (неподдерживаемые типы или дубликаты)
};

// Семантический анализатор

struct Program;
struct Stmt;
struct Expr;
struct Block;
struct ImportDecl;

class SemanticAnalyzer {
    SymbolTable table;  //  Создание таблицы символов
    std::vector<std::string> errors;    //  Вектор ошибок
    std::shared_ptr<Type> currentReturnType;  //  Тип возврата текущей функции 
    int loopDepth = 0;                         //  Глубина вложенности циклов (для break/continue)
    std::string currentFilePath;               //  Путь текущего файла (для разрешения import)
    std::unordered_set<std::string> importedFiles;  //  Множество уже импортированных файлов (защита от циклов)

    void registerBuiltins();                                    //  Регистрация print, len, input, exit, panic
    void collectTopLevel(const std::vector<Stmt*>& decls);      //  Первый проход — собираем имена top-level объявлений
    std::shared_ptr<Type> resolveTypeName(const std::string& name);  //  Преобразование строки типа в Type
    void processImport(ImportDecl* imports);                        //  Загрузка и анализ импортируемого файла
    void processCImport(ImportDecl* imp);                           //  Парсинг C-заголовка через libclang

    void error(int line, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":0: error: " + message);
    }

    void error(int line, int column, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":" + std::to_string(column) + ": error: " + message);
    }

    std::shared_ptr<Type> analyzeExpr(Expr* expr, std::shared_ptr<Type> expected = nullptr);  // expected — ожидаемый тип из контекста для контекстной типизации литералов
    void analyzeStmt(Stmt* stmt);                   // Анализ одной инструкции
    void analyzeBlock(Block* block);                // Анализ блока (вход/выход из scope)

public:
    std::expected<void, std::string> analyze(Program* program, const std::string& filePath);  // Точка входа
};
