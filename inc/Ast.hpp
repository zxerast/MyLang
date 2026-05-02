#pragma once

#include "Tokens.hpp"
#include "Type.hpp"
#include <expected>
#include <memory>

//  Forward declarations для семантических привязок (заполняются SemanticAnalyzer
//  и используются кодгеном без повторного lookup'а).
struct Symbol;
struct FuncInfo;
struct FieldInfo;

struct ASTNode{
    int line = 0;       // Номер строки в исходнике (из токена)
    int column = 0;     // Номер столбца (из токена)
    virtual ~ASTNode() = default;
};

struct Expr : ASTNode {     //  Возвращает значение
    std::shared_ptr<Type> resolvedType;  //  Тип, определённый семантическим анализатором
};
struct Stmt : ASTNode {};   //  Выполнят действие

//  Выражения

struct Number : Expr{
    double value;
    bool isFloat = false;  //  true если литерал содержит точку
};

struct String : Expr{
    std::string value;
};

struct Bool : Expr{
    bool value;
};

struct Identifier : Expr{
    std::string name;
    //  Семантическая привязка: либо разрешённый символ (переменная/параметр/функция/тип),
    //  либо поле текущего класса (неявное self.<name>). Заполняется SemanticAnalyzer.
    std::shared_ptr<Symbol> resolvedSym;
    FieldInfo* resolvedField = nullptr;
};

enum class Operand{
    Add, Sub, Mul, Div, Mod,
    EqualEqual, NotEqual,
    Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not, Pow,
    UnaryPlus, UnaryMinus,
    Increment, Decrement,
};

struct Binary : Expr{
    Operand op;
    Expr *left;
    Expr *right;
};

struct Unary : Expr{
    Operand op;
    Expr *operand;
};

struct FuncCall : Expr{
    Expr *callee;
    std::vector<Expr*> args;
    bool isExternC = false;   //  Вызов C-функции (без префикса lang_)
    bool isVariadic = false;  //  C-вариадная (нужен xor eax, eax)
    //  Семантическая привязка: для прямых вызовов и конструкторов класса — символ цели,
    //  для вызовов методов класса — найденный FuncInfo метода.
    std::shared_ptr<Symbol> resolvedCallee;
    std::shared_ptr<FuncInfo> resolvedMethod;
};

struct FieldAccess : Expr{
    Expr *object;
    std::string field;
    //  Семантическая привязка: либо поле struct/class, либо метод класса.
    FieldInfo* resolvedField = nullptr;
    std::shared_ptr<FuncInfo> resolvedMethod;
    bool isTypeDefaultFieldAccess = false;
};

struct ArrayAccess : Expr{
    Expr *object;
    Expr *index;
};

struct ArrayLiteral : Expr{
    std::vector<Expr*> elements;
};

struct FieldInit{
    std::string name;
    Expr *value;
};

struct StructLiteral : Expr{
    std::string name;
    std::vector<FieldInit> fields;
};


struct NamespaceAccess : Expr{
    std::string nameSpace;
    std::string member;
    //  Семантическая привязка: разрешённый символ из соответствующего namespace.
    std::shared_ptr<Symbol> resolvedSym;
};

//  Инструкции

enum class AssignOp {
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign
};

struct Assign : Stmt{
    AssignOp op = AssignOp::Assign;
    Expr *target;
    Expr *value;
};

struct Block : Stmt{
    std::vector<Stmt*> statements;
};

struct If : Stmt{
    Expr *condition;
    Stmt *thenBranch;
    Stmt *elseBranch;
};

struct While : Stmt{
    Expr *condition;
    Stmt *body;
};

struct Break : Stmt{};

struct Continue : Stmt{};

struct Return : Stmt{
    Expr *value;
};

struct ExprStmt : Stmt{
    Expr *expr;
};

struct TypeSuffix {
    bool isDynamic = false;   // true для []
    Expr* size = nullptr;     // nullptr для [], Expr* для [expr]
};

struct TypeName {
    std::string base;                 // int, Point, string, ...
    std::vector<TypeSuffix> suffixes; // [], [expr], [3], [n + 1], ...
};

struct VarInit {
    std::string name;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    Expr* init = nullptr;
};

struct VarDecl : Stmt{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;   // пустая при isAuto == true
    std::vector<VarInit*> vars;
};

struct CastExpr : Expr{
    TypeName *targetType = nullptr;
    Expr *value;
};


//  Объявления верхнего уровня 

struct Param{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;
    std::string name;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    Expr *defaultValue = nullptr;
};

struct FuncDecl : Stmt{
    TypeName *returnType = nullptr;
    std::string name;
    std::vector<Param> params;
    Block *body;
    std::shared_ptr<Symbol> resolvedSym = nullptr;
    std::shared_ptr<FuncInfo> resolvedInfo = nullptr;
};

struct StructField{
    bool isConst = false;
    bool isAuto = false;
    TypeName *typeName = nullptr;
    std::string name;
    Expr* defaultValue = nullptr;  //  Значение по умолчанию 
    std::shared_ptr<Type> resolvedType = nullptr;
};

struct StructDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;
};

struct ClassDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;       //  Поля класса
    std::vector<FuncDecl*> methods;        //  Методы
    std::vector<StructDecl*> structs;      // Вложенные структуры
    FuncDecl* constructor = nullptr;       //  Конструктор (имя = имя класса)
    FuncDecl* destructor = nullptr;        //  Деструктор (~имя класса)
};

struct TypeAlias : Stmt{
    std::string alias;
    TypeName *original = nullptr;
};

struct NamespaceDecl : Stmt{
    std::string name;
    std::vector<Stmt*> decls;
};

struct ImportDecl : Stmt{
    std::string path;   // "math.lang" или "stdio.h"
    bool isC = false;   // true для import <header.h>
};

struct ExportDecl : Stmt{
    Stmt *decl;         // обёрнутое объявление
};

// Узел для литерала null
struct NullLiteral : Expr {};

// Узел для символьного литерала
struct CharLiteral : Expr {
    char value;
};

struct Program : ASTNode{
    std::vector<Stmt*> imports;
    std::vector<Stmt*> decls;
};

// Функции

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source, const std::string& filePath = "<source>");
