module;

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

export module ast;

import types;

export struct Symbol;
export struct FuncInfo;
export struct FieldInfo;

export struct ASTNode{
    int line = 0;       // Номер строки в исходнике (из токена)
    int column = 0;     // Номер столбца (из токена)
    virtual ~ASTNode() = default;
};

export struct Expr : ASTNode {     //  Возвращает значение
    std::shared_ptr<Type> resolvedType;  //  Тип, определённый семантическим анализатором
};
export struct Stmt : ASTNode {};   //  Выполнят действие

//  Выражения

export struct Number : Expr{
    double value;
    bool isFloat = false;  //  true если литерал содержит точку
};

export struct String : Expr{
    std::string value;
};

export struct Bool : Expr{
    bool value;
};

export struct Identifier : Expr{
    std::string name;
    //  Семантическая привязка: либо разрешённый символ (переменная/параметр/функция/тип),
    //  либо поле текущего класса (неявное self.<name>). Заполняется SemanticAnalyzer.
    std::shared_ptr<Symbol> resolvedSym;
    FieldInfo* resolvedField = nullptr;
};

export enum class Operand{
    Add, Sub, Mul, Div, Mod,
    EqualEqual, NotEqual,
    Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not, Pow,
    UnaryPlus, UnaryMinus,
    Increment, Decrement,
};

export struct Binary : Expr{
    Operand op;
    Expr *left;
    Expr *right;
};

export struct Unary : Expr{
    Operand op;
    Expr *operand;
};

export struct FuncCall : Expr{
    Expr *callee;
    std::vector<Expr*> args;
    bool isExternC = false;   //  Вызов C-функции (без префикса lang_)
    bool isVariadic = false;  //  C-вариадная (нужен xor eax, eax)
    //  Семантическая привязка: для прямых вызовов и конструкторов класса — символ цели,
    //  для вызовов методов класса — найденный FuncInfo метода.
    std::shared_ptr<Symbol> resolvedCallee;
    std::shared_ptr<FuncInfo> resolvedMethod;
};

export struct FieldAccess : Expr{
    Expr *object;
    std::string field;
    //  Семантическая привязка: либо поле struct/class, либо метод класса.
    FieldInfo* resolvedField = nullptr;
    std::shared_ptr<FuncInfo> resolvedMethod;
    bool isTypeDefaultFieldAccess = false;
};

export struct ArrayAccess : Expr{
    Expr *object;
    Expr *index;
};

export struct ArrayLiteral : Expr{
    std::vector<Expr*> elements;
};

export struct FieldInit{
    std::string name;
    Expr *value;
};

export struct StructLiteral : Expr{
    std::string name;
    std::vector<FieldInit> fields;
};


export struct NamespaceAccess : Expr{
    std::string nameSpace;
    std::string member;
    //  Семантическая привязка: разрешённый символ из соответствующего namespace.
    std::shared_ptr<Symbol> resolvedSym;
};

//  Инструкции

export enum class AssignOp {
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign
};

export struct Assign : Stmt{
    AssignOp op = AssignOp::Assign;
    Expr *target;
    Expr *value;
};

export struct Block : Stmt{
    std::vector<Stmt*> statements;
};

export struct If : Stmt{
    Expr *condition;
    Stmt *thenBranch;
    Stmt *elseBranch;
};

export struct While : Stmt{
    Expr *condition;
    Stmt *body;
};

export struct Break : Stmt{};

export struct Continue : Stmt{};

export struct Return : Stmt{
    Expr *value;
};

export struct ExprStmt : Stmt{
    Expr *expr;
};

export struct TypeSuffix {
    bool isDynamic = false;   // true для []
    Expr* size = nullptr;     // nullptr для [], Expr* для [expr]
};

export struct TypeName {
    std::string base;                 // int, Point, string, ...
    std::vector<TypeSuffix> suffixes; // [], [expr], [3], [n + 1], ...
};

export struct VarInit {
    std::string name;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    Expr* init = nullptr;
};

export struct VarDecl : Stmt{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;   // пустая при isAuto == true
    std::vector<VarInit*> vars;
};

export struct CastExpr : Expr{
    TypeName *targetType = nullptr;
    Expr *value;
};


//  Объявления верхнего уровня 

export struct Param{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;
    std::string name;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    Expr *defaultValue = nullptr;
};

export struct FuncDecl : Stmt{
    TypeName *returnType = nullptr;
    std::string name;
    std::vector<Param> params;
    Block *body;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    std::shared_ptr<FuncInfo> resolvedInfo = nullptr;
};

export struct StructField{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;
    std::string name;
    Expr* defaultValue = nullptr;  //  Значение по умолчанию 
    std::shared_ptr<Type> resolvedType = nullptr;
};

export struct StructDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;
};

export struct ClassDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;       //  Поля класса
    std::vector<FuncDecl*> methods;        //  Методы
    std::vector<StructDecl*> structs;      // Вложенные структуры
    FuncDecl* constructor = nullptr;       //  Конструктор (имя = имя класса)
    FuncDecl* destructor = nullptr;        //  Деструктор (~имя класса)
};

export struct TypeAlias : Stmt{
    std::string alias;
    TypeName *original = nullptr;
};

export struct NamespaceDecl : Stmt{
    std::string name;
    std::vector<Stmt*> decls;
};

export struct ImportDecl : Stmt{
    std::string path;   // "math.lang" или "stdio.h"
    bool isC = false;   // true для import <header.h>
};

export struct ExportDecl : Stmt{
    Stmt *decl;         // обёрнутое объявление
};

// Узел для литерала null
export struct NullLiteral : Expr {};

// Узел для символьного литерала
export struct CharLiteral : Expr {
    char value;
};

export struct Program : ASTNode{
    std::vector<Stmt*> imports;
    std::vector<Stmt*> decls;
};

export struct Scope;

export enum class SymbolKind {
    Variable,
    Function,
    Struct,
    Class,
    TypeAlias,
    Namespace,
};

export struct FieldInfo {
    std::string name;
    std::shared_ptr<Type> type;
    bool isConst = false;
    Expr* defaultValue = nullptr;
};

export struct StructInfo {
    std::string name;
    std::vector<FieldInfo> fields;
};

export struct ClassInfo {
    std::string name;
    std::vector<FieldInfo> fields;
    std::unordered_map<std::string, std::shared_ptr<FuncInfo>> methods;
    std::unordered_map<std::string, std::shared_ptr<StructInfo>> nestedStructs;
    std::shared_ptr<FuncInfo> constructor = nullptr;
    std::shared_ptr<FuncInfo> destructor = nullptr;
};

export struct ParamInfo {
    std::string name;
    std::shared_ptr<Type> type;

    Expr* defaultValue = nullptr;
    bool isConst = false;
};

export struct FuncInfo {
    std::shared_ptr<Type> returnType;
    std::vector<ParamInfo> params;
    bool isExternC = false;
    bool isVariadic = false;
};

export struct Symbol {
    std::string name;
    SymbolKind kind;
    std::shared_ptr<Type> type;

    bool isConst = false;
    bool isExported = false;
    bool isInitialized = false;
    bool isAuto = false;
    TypeName* aliasTarget = nullptr;
    bool isResolvingAlias = false;
    Expr* autoInit = nullptr;
    bool isResolvingAuto = false;
    std::optional<long long> intConstValue = std::nullopt;

    std::shared_ptr<FuncInfo> funcInfo = nullptr;
    std::shared_ptr<StructInfo> structInfo = nullptr;
    std::shared_ptr<ClassInfo> classInfo = nullptr;
    std::shared_ptr<Scope> namespaceScope = nullptr;
};

export struct Scope {
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;
    std::shared_ptr<Scope> parent = nullptr;
};
