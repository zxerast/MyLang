module;

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

export module semantic;

import ast;
import symbol_table;
import types;

export enum class DeclContext {
    Variable,
    Field,
    Parameter
};

export class SemanticAnalyzer {
    SymbolTable table;
    std::vector<std::string> errors;
    std::shared_ptr<Type> currentReturnType;
    int loopDepth = 0;
    std::string currentFilePath;
    std::unordered_set<std::string> importedFiles;
    std::string currentNamespace;

    std::shared_ptr<ClassInfo> currentClass = nullptr;
    std::unordered_set<Stmt*> invalidTopLevelDecls;

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
    void registerBuiltins();
    bool declareTopLevelSymbol(std::shared_ptr<Symbol> sym, Stmt* decl, bool allowNamespaceMerge = false);
    void predeclareTopLevel(const std::vector<Stmt*>& decls);
    void collectTopLevel(const std::vector<Stmt*>& decls);
    std::shared_ptr<Type> resolveTypeName(TypeName *typeName);

    std::expected<void, std::string> analyzeModule(Program* program, const std::string& filePath, bool requireMain);

    void importExportedSymbolsFrom(SemanticAnalyzer& module);

    void processImport(ImportDecl* imports, Program* ownerProgram);
    void processCImport(ImportDecl* imp);

    void error(int line, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":0: error: " + message);
    }

    void error(int line, int column, const std::string& message) {
        errors.push_back(currentFilePath + ":" + std::to_string(line) + ":" + std::to_string(column) + ": error: " + message);
    }

    std::string nonValueSymbolMessage(const std::shared_ptr<Symbol>& sym, const std::string& name) const;
    std::shared_ptr<Type> analyzeExpr(Expr* expr, std::shared_ptr<Type> expected = nullptr);
    void analyzeStmt(Stmt* stmt);
    void analyzeBlock(Block* block);
    std::shared_ptr<Symbol> resolveTargetRoot(Expr* e);
    bool isLvalue(Expr* e);

    std::shared_ptr<Symbol> resolveQualifiedSymbol(const std::string& nameSpace, const std::string& member);
    std::shared_ptr<Symbol> resolveQualifiedSymbol(const std::string& qualifiedName);

    void checkDuplicateParams(const std::vector<Param>& params, int line, int column, const std::string& where);
    void checkDuplicateFields(const std::vector<StructField>& fields, int line, int column, const std::string& where);
    void checkDuplicateMethods(const std::vector<FuncDecl*>& methods, int line, int column, const std::string& className);
    void checkDuplicateNestedStructs(const std::vector<StructDecl*>& structs, int line, int column, const std::string& className);

    std::shared_ptr<Type> resolveDeclaredType(bool isAuto, bool isConst, TypeName* typeName, Expr*& defaultValue, int line, int column, const std::string& what, DeclContext context);

    void checkCallArguments(const std::string& what, const std::vector<ParamInfo>& params, const std::vector<std::shared_ptr<Type>>& argTypes, bool variadic, int line, int column, bool isExternC = false);
    void appendMissingDefaultArgs(FuncCall* call, const std::vector<ParamInfo>& params, bool variadic);
    Expr* makeDefaultExprForType(const std::shared_ptr<Type>& type, int line, int column);

public:
    std::expected<void, std::string> analyze(Program* program, const std::string& filePath);
};
