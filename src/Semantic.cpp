#include "SymbolTable.hpp"
#include "Ast.hpp"
#include <expected>

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

// --- Обход AST ---

static SymbolTable table;
static std::vector<std::string> errors;

void analyzeExpr(Expr* expr) {  //  Смотрим выражение
    if (!expr) return;  //  В случае int x -> ничего не делаем всё ок

    if (auto* id = dynamic_cast<Identifier*>(expr)) {   //  Идентификатор — проверяем что объявлен в таблице
        if (!table.resolve(id->name))
            errors.push_back("'" + id->name + "' is not declared");
    }
    else if (auto* bin = dynamic_cast<Binary*>(expr)) {  //  Бинарная операция — проверяем оба операнда
        analyzeExpr(bin->left);
        analyzeExpr(bin->right);
    }
    else if (auto* un = dynamic_cast<Unary*>(expr)) {  //  Унарная операция — проверяем операнд
        analyzeExpr(un->operand);
    }
    else if (auto* call = dynamic_cast<FuncCall*>(expr)) {  //  Вызов функции — проверяем имя и все аргументы
        analyzeExpr(call->callee);
        for (auto* arg : call->args)
            analyzeExpr(arg);
    }
    else if (auto* field = dynamic_cast<FieldAccess*>(expr)) {  //  Доступ к полю (p.x) — проверяем объект
        analyzeExpr(field->object);
    }
    else if (auto* arr = dynamic_cast<ArrayAccess*>(expr)) {  //  Индексация (a[i]) — проверяем массив и индекс
        analyzeExpr(arr->object);
        analyzeExpr(arr->index);
    }
    else if (auto* arrLit = dynamic_cast<ArrayLiteral*>(expr)) {  //  Литерал массива — проверяем каждый элемент
        for (auto* elem : arrLit->elements)
            analyzeExpr(elem);
    }
    else if (auto* sLit = dynamic_cast<StructLiteral*>(expr)) {  //  Литерал структуры — проверяем значения полей
        for (auto& field : sLit->fields)
            analyzeExpr(field.value);
    }
    else if (auto* cast = dynamic_cast<CastExpr*>(expr)) {  //  cast<T>(expr) — проверяем выражение внутри
        analyzeExpr(cast->value);
    }
    else if (auto* ns = dynamic_cast<NamespaceAccess*>(expr)) {  //  Math::PI — проверяем что namespace существует
        if (!table.resolve(ns->nameSpace))
            errors.push_back("'" + ns->nameSpace + "' is not declared");
    }
    //  Number, String, Bool — литералы, проверять нечего
}

void analyzeBlock(Block* block) {
    table.enterScope();
    for (auto* s : block->statements)   //  Смотрим стэйтменты внутри текущей области
        analyzeStmt(s);
    table.exitScope();
}

void analyzeStmt(Stmt* stmt) {
    if (auto* var = dynamic_cast<VarDecl*>(stmt)) { //  Если это объявление переменной
        auto sym = std::make_shared<Symbol>();      //  То сперва всё записываем
        sym->name = var->name;
        sym->kind = SymbolKind::Variable;
        sym->isConst = var->isConst;
        sym->isInitialized = (var->init != nullptr);

        analyzeExpr(var->init);     //  Затем смотрим чё в неё пишем

        auto result = table.declare(sym);   //  И сохраняем её в таблицу
        if (!result)
            errors.push_back(result.error());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {    //  Если это блок то соответственно анализируем блок
        analyzeBlock(block);
    }
    else if (auto* assign = dynamic_cast<Assign*>(stmt)) {  //  Присваивание: проверяем левую и правую части
        analyzeExpr(assign->target);
        analyzeExpr(assign->value);
    }
    else if (auto* ifStmt = dynamic_cast<If*>(stmt)) {  //  if — проверяем условие и обе ветки
        analyzeExpr(ifStmt->condition);
        analyzeStmt(ifStmt->thenBranch);
        if (ifStmt->elseBranch)
            analyzeStmt(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<While*>(stmt)) {  //  while — проверяем условие и тело
        analyzeExpr(whileStmt->condition);
        analyzeStmt(whileStmt->body);
    }
    else if (auto* ret = dynamic_cast<Return*>(stmt)) {  //  return — проверяем возвращаемое выражение
        analyzeExpr(ret->value);
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {  //  выражение как инструкция (напр. print(x);)
        analyzeExpr(exprStmt->expr);
    }
    else if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {  //  Объявление функции
        //  Регистрируем имя функции в текущем scope
        auto sym = std::make_shared<Symbol>();
        sym->name = func->name;
        sym->kind = SymbolKind::Function;

        auto result = table.declare(sym);
        if (!result)
            errors.push_back(result.error());

        //  Тело функции — новый scope, в который добавляем параметры
        table.enterScope();
        for (auto& param : func->params) {
            auto pSym = std::make_shared<Symbol>();
            pSym->name = param.name;
            pSym->kind = SymbolKind::Variable;
            pSym->isInitialized = true;  //  параметры всегда инициализированы

            auto pResult = table.declare(pSym);
            if (!pResult)
                errors.push_back(pResult.error());
        }
        //  Анализируем тело без лишнего enterScope — Block сам его создаст
        for (auto* s : func->body->statements)
            analyzeStmt(s);
        table.exitScope();
    }
    else if (auto* structDecl = dynamic_cast<StructDecl*>(stmt)) {  //  Объявление структуры
        auto sym = std::make_shared<Symbol>();
        sym->name = structDecl->name;
        sym->kind = SymbolKind::Struct;

        auto result = table.declare(sym);
        if (!result)
            errors.push_back(result.error());
    }
    else if (auto* alias = dynamic_cast<TypeAlias*>(stmt)) {  //  type alias
        auto sym = std::make_shared<Symbol>();
        sym->name = alias->alias;
        sym->kind = SymbolKind::TypeAlias;

        auto result = table.declare(sym);
        if (!result)
            errors.push_back(result.error());
    }
    else if (auto* ns = dynamic_cast<NamespaceDecl*>(stmt)) {  //  namespace — регистрируем имя и анализируем содержимое
        auto sym = std::make_shared<Symbol>();
        sym->name = ns->name;
        sym->kind = SymbolKind::Namespace;

        auto result = table.declare(sym);
        if (!result)
            errors.push_back(result.error());

        table.enterScope();
        for (auto* decl : ns->decls)
            analyzeStmt(decl);
        table.exitScope();
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(stmt)) {  //  export — просто анализируем обёрнутое объявление
        analyzeStmt(exp->decl);
    }
    //  Break, Continue — на этом этапе просто пропускаем
}

std::expected<void, std::string> analyze(Program* program) {    //  Точка входа анализатора
    errors.clear();

    for (auto* decl : program->decls)
        analyzeStmt(decl);

    if (!errors.empty()) {
        std::string msg;
        for (auto& e : errors)
            msg += e + "\n";
        return std::unexpected(msg);
    }
    return {};
}
