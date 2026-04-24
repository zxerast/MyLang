#pragma once

#include "Tokens.hpp"
#include "Type.hpp"
#include <expected>
#include <memory>

struct ASTNode{
    int line = 0;
    int column = 0;
    virtual ~ASTNode() = default;
};

struct Expr : ASTNode {
    std::shared_ptr<Type> resolvedType;
};
struct Stmt : ASTNode {};

//  Выражения

struct Number : Expr{
    double value;
    bool isFloat = false;
};

struct String : Expr{
    std::string value;
};

struct Bool : Expr{
    bool value;
};

struct CharLit : Expr{
    char value;
};

struct NullLit : Expr{};

struct Identifier : Expr{
    std::string name;
};

enum class Operand{
    Add, Sub, Mul, Div, Mod,
    EqualEqual, NotEqual,
    Less, Greater, LessEqual, GreaterEqual,
    And, Or, Not,
    UnaryPlus, UnaryMinus,
    Increment, Decrement,
};

struct Binary : Expr{
    Operand op;
    Expr* left = nullptr;
    Expr* right = nullptr;
};

struct Unary : Expr{
    Operand op;
    Expr* operand = nullptr;
};

struct FuncCall : Expr{
    Expr* callee = nullptr;
    std::vector<Expr*> args;
    bool isExternC = false;
    bool isVariadic = false;
};

struct FieldAccess : Expr{
    Expr* object = nullptr;
    std::string field;
};

struct ArrayAccess : Expr{
    Expr* object = nullptr;
    Expr* index = nullptr;
};

struct ArrayLiteral : Expr{
    std::vector<Expr*> elements;
};

struct FieldInit{
    std::string name;
    Expr* value = nullptr;
};

struct StructLiteral : Expr{
    std::string name;
    std::vector<FieldInit> fields;
};

struct CastExpr : Expr{
    std::string targetType;
    Expr* value = nullptr;
};

struct NamespaceAccess : Expr{
    std::string nameSpace;  // может быть "A::B" для вложенных
    std::string member;
};

struct NewExpr : Expr{
    std::string className;
    std::vector<Expr*> args;
};

//  Инструкции

struct Assign : Stmt{
    Expr* target = nullptr;
    Expr* value = nullptr;
};

struct Block : Stmt{
    std::vector<Stmt*> statements;
};

struct If : Stmt{
    Expr* condition = nullptr;
    Stmt* thenBranch = nullptr;
    Stmt* elseBranch = nullptr;
};

struct While : Stmt{
    Expr* condition = nullptr;
    Stmt* body = nullptr;
};

struct Break : Stmt{};

struct Continue : Stmt{};

struct Return : Stmt{
    Expr* value = nullptr;
};

struct ExprStmt : Stmt{
    Expr* expr = nullptr;
};

struct DeleteStmt : Stmt{
    Expr* value = nullptr;
};

struct VarDecl : Stmt{
    bool isConst = false;
    bool isAuto = false;
    std::string typeName;
    std::string name;
    Expr* init = nullptr;
};

//  Объявления верхнего уровня

struct Param{
    std::string typeName;
    std::string name;
    Expr* defaultValue = nullptr;
    bool isConst = false;
};

struct FuncDecl : Stmt{
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    Block* body = nullptr;
};

struct StructField{
    std::string typeName;
    std::string name;
    Expr* defaultValue = nullptr;
};

struct StructDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;
};

struct ClassDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;
    std::vector<FuncDecl*> methods;
    std::vector<StructDecl*> nestedStructs;
    FuncDecl* constructor = nullptr;
    FuncDecl* destructor = nullptr;
};

struct TypeAlias : Stmt{
    std::string alias;
    std::string original;
};

struct NamespaceDecl : Stmt{
    std::string name;
    std::vector<Stmt*> decls;
};

struct ImportDecl : Stmt{
    std::string path;
    bool isC = false;
};

struct ExportDecl : Stmt{
    Stmt* decl = nullptr;
};

struct Program : ASTNode{
    std::vector<Stmt*> imports;
    std::vector<Stmt*> decls;
};

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source, const std::string& filePath = "<source>");
