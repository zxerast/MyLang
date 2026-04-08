#include "Tokens.hpp"
#include "Type.hpp"
#include <expected>
#include <fstream>
#include <memory>

struct ASTNode{
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
};

struct FieldAccess : Expr{
    Expr *object;
    std::string field;
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

struct CastExpr : Expr{
    std::string targetType;
    Expr *value;
};

struct NamespaceAccess : Expr{
    std::string nameSpace;
    std::string member;
};

//  Инструкции

struct Assign : Stmt{
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

struct VarDecl : Stmt{
    bool isConst = false;
    bool isAuto = false;
    std::string typeName;   // пустая при isAuto == true
    std::string name;
    Expr *init = nullptr;
};

//  Объявления верхнего уровня 

struct Param{
    std::string typeName;
    std::string name;
};

struct FuncDecl : Stmt{
    std::string returnType;
    std::string name;
    std::vector<Param> params;
    Block *body;
};

struct StructField{
    std::string typeName;
    std::string name;
};

struct StructDecl : Stmt{
    std::string name;
    std::vector<StructField> fields;
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
    std::string path;   // "math.lang"
};

struct ExportDecl : Stmt{
    Stmt *decl;         // обёрнутое объявление
};

struct Program : ASTNode{
    std::vector<Stmt*> imports;
    std::vector<Stmt*> decls;
};

// Функции 

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source);
std::expected<void, std::string> compile(Expr *head, std::ofstream &file);
std::expected<void, std::string> generate(const std::vector<Expr*>& nodes, std::ofstream &file);