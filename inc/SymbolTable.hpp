#pragma once

#include "Type.hpp"
#include "Ast.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <optional>
#include <expected>

struct Scope;     //  forward declaration для namespace_scope в Symbol
struct FuncInfo;  //  forward declaration для ClassInfo
struct TypeName;  //  forward declaration для resolveTypeName
struct TypeSuffix;       //  forward declaration для resolveArrayTypeSuffix
struct Expr;             //  forward declaration для default-значений полей/параметров

enum class SymbolKind { //  Что есть наш символ
    Variable,
    Function,
    Struct,
    Class,
    TypeAlias,
    Namespace,
};

struct FieldInfo {  //  Поле структуры/класса (по аналогии с ParamInfo для параметров)
    std::string name;
    std::shared_ptr<Type> type;
    bool isConst = false;
    //  Объявленное default-выражение поля. Runtime-переопределение `TypeName.field = value`
    //  хранится в default-слоте кодгена и не мутирует эту semantic-модель.
    Expr* defaultValue = nullptr;
};

struct StructInfo { //  Структура
    std::string name;   //  Имя структуры
    std::vector<FieldInfo> fields;
};

struct ClassInfo {  //  Класс
    std::string name;   //  Имя класса
    std::vector<FieldInfo> fields;  //  Поля как в структуре
    std::unordered_map<std::string, std::shared_ptr<FuncInfo>> methods;  //  Методы
    std::unordered_map<std::string, std::shared_ptr<StructInfo>> nestedStructs;  //  Вложенные структуры
    std::shared_ptr<FuncInfo> constructor = nullptr;    //  Указатель на конструктор
    std::shared_ptr<FuncInfo> destructor = nullptr;     //  Указатель на деструктор (если объявлен)
};

struct ParamInfo {
    std::string name;
    std::shared_ptr<Type> type;

    Expr* defaultValue = nullptr;       // nullptr => default по типу
    bool isConst = false;
};

struct FuncInfo {   //  Функция
    std::shared_ptr<Type> returnType;   //  Чё возвращает
    std::vector<ParamInfo> params;  // имя параметра -> тип
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
    bool isAuto = false;
    TypeName* aliasTarget = nullptr;
    bool isResolvingAlias = false;
    Expr* autoInit = nullptr;
    bool isResolvingAuto = false;
    std::optional<long long> intConstValue = std::nullopt;

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
        //  По спецификации повторное объявление в том же scope — допустимое shadowing,
        //  новое объявление перекрывает старое. Не возвращаем ошибку.
        current->symbols[sym->name] = sym;
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

    std::shared_ptr<Symbol> resolveCurrentScope(const std::string& name) {
        auto it = current->symbols.find(name);

        if (it != current->symbols.end()) {
            return it->second;
        }

        return nullptr; 
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

enum class DeclContext {
    Variable,
    Field,
    Parameter
};

class SemanticAnalyzer {
    SymbolTable table;  //  Создание таблицы символов
    std::vector<std::string> errors;    //  Вектор ошибок
    std::shared_ptr<Type> currentReturnType;  //  Тип возврата текущей функции 
    int loopDepth = 0;                         //  Глубина вложенности циклов (для break/continue)
    std::string currentFilePath;               //  Путь текущего файла (для разрешения import)
    std::unordered_set<std::string> importedFiles;  //  Множество уже импортированных файлов (защита от циклов)
    std::string currentNamespace;              //  Квалификатор текущего namespace при сборе/анализе

    std::shared_ptr<ClassInfo> currentClass = nullptr;  //  Класс, чьё тело анализируется (для разрешения self-полей)
    std::unordered_set<Stmt*> invalidTopLevelDecls;      //  top-level объявления, отвергнутые на predeclare-проходе

    std::shared_ptr<Type> resolveArrayTypeSuffix(
        std::shared_ptr<Type> base,
        const TypeSuffix& suffix,
        int line,
        int column
    );

    std::optional<long long> evalConstIntExpr(Expr* expr);
    std::shared_ptr<Type> ensureVariableTypeKnown(const std::shared_ptr<Symbol>& sym, int line, int column);
    std::shared_ptr<Type> ensureAliasTypeKnown(const std::shared_ptr<Symbol>& sym, int line, int column);
    
    bool checkArraySizeExpr(Expr* sizeExpr, int line, int column);
    void registerBuiltins();                                    //  Регистрация print, len, input, exit, panic
    bool declareTopLevelSymbol(std::shared_ptr<Symbol> sym, Stmt* decl, bool allowNamespaceMerge = false);
    void predeclareTopLevel(const std::vector<Stmt*>& decls);
    void collectTopLevel(const std::vector<Stmt*>& decls);      //  Первый проход — собираем имена top-level объявлений
    std::shared_ptr<Type> resolveTypeName(TypeName *typeName);  //  Преобразование строки типа в Type
    
    std::expected<void, std::string> analyzeModule(Program* program, const std::string& filePath, bool requireMain);

    void importExportedSymbolsFrom(SemanticAnalyzer& module);

    void processImport(ImportDecl* imports, Program* ownerProgram); //  Загрузка и анализ импортируемого файла
    void processCImport(ImportDecl* imp);                           //  Парсинг C-заголовка через libclang

    void error(int line, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":0: error: " + message);
    }

    void error(int line, int column, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":" + std::to_string(column) + ": error: " + message);
    }

    std::string nonValueSymbolMessage(const std::shared_ptr<Symbol>& sym, const std::string& name) const;
    std::shared_ptr<Type> analyzeExpr(Expr* expr, std::shared_ptr<Type> expected = nullptr);  // expected — ожидаемый тип из контекста для контекстной типизации литералов
    void analyzeStmt(Stmt* stmt);                   // Анализ одной инструкции
    void analyzeBlock(Block* block);                // Анализ блока (вход/выход из scope)
    std::shared_ptr<Symbol> resolveTargetRoot(Expr* e);  //  Корневой Symbol для lvalue (для проверки const)
    bool isLvalue(Expr* e);  //  Проверка, что выражение — корректный lvalue (присваивание, ++/--)

    std::shared_ptr<Symbol> resolveQualifiedSymbol(const std::string& nameSpace, const std::string& member);
    std::shared_ptr<Symbol> resolveQualifiedSymbol(const std::string& qualifiedName);

    void checkDuplicateParams(const std::vector<Param>& params, int line, int column, const std::string& where);
    void checkDuplicateFields(const std::vector<StructField>& fields, int line, int column, const std::string& where);
    void checkDuplicateMethods(const std::vector<FuncDecl*>& methods, int line, int column, const std::string& className);
    void checkDuplicateNestedStructs(const std::vector<StructDecl*>& structs, int line, int column, const std::string& className);

    //  Резолвит тип объявления (поля/параметра) с поддержкой auto и проверкой default-значения.
    //  Если isConst и явного default нет — выдаётся ошибка (auto-fill default не применяется).
    std::shared_ptr<Type> resolveDeclaredType(bool isAuto, bool isConst, TypeName* typeName, Expr*& defaultValue, int line, int column, const std::string& what, DeclContext context);

    //  Унифицированная проверка аргументов вызова (функция/метод) по сигнатуре:
    //  арность (с учётом variadic) и widening через isImplicitlyConvertible.
    void checkCallArguments(const std::string& what, const std::vector<ParamInfo>& params, const std::vector<std::shared_ptr<Type>>& argTypes, bool variadic, int line, int column, bool isExternC = false);
    void appendMissingDefaultArgs(FuncCall* call, const std::vector<ParamInfo>& params, bool variadic);
    Expr* makeDefaultExprForType(const std::shared_ptr<Type>& type, int line, int column);

public:
    std::expected<void, std::string> analyze(Program* program, const std::string& filePath);  // Точка входа
};
