#include "Ast.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <variant>
#include <expected>
#include <cmath>

// ======================== Значения ========================

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;
using ValueBase = std::variant<double, bool, std::string, Array, Object>;

struct Value : ValueBase {
    using ValueBase::ValueBase;
    using ValueBase::operator=;
};

std::string valueToString(const Value& val){
    if (auto *d = std::get_if<double>(&val)){
        if (*d == (int64_t)*d) return std::to_string((int64_t)*d);
        return std::to_string(*d);
    }
    if (auto *b = std::get_if<bool>(&val)){
        return *b ? "true" : "false";
    }
    if (auto *s = std::get_if<std::string>(&val)){
        return *s;
    }
    if (auto *arr = std::get_if<std::vector<Value>>(&val)){
        std::string result = "[";
        for (size_t i = 0; i < arr->size(); i++){
            if (i > 0) result += ", ";
            result += valueToString((*arr)[i]);
        }
        return result + "]";
    }
    if (auto *obj = std::get_if<std::map<std::string, Value>>(&val)){
        std::string result = "{";
        bool first = true;
        for (auto& [k, v] : *obj){
            if (!first) result += ", ";
            result += k + ": " + valueToString(v);
            first = false;
        }
        return result + "}";
    }
    return "???";
}

double toNumber(const Value& val){
    if (auto *d = std::get_if<double>(&val)) return *d;
    if (auto *b = std::get_if<bool>(&val)) return *b ? 1.0 : 0.0;
    return 0.0;
}

bool toBool(const Value& val){
    if (auto *b = std::get_if<bool>(&val)) return *b;
    if (auto *d = std::get_if<double>(&val)) return *d != 0.0;
    if (auto *s = std::get_if<std::string>(&val)) return !s->empty();
    return true;
}

// ======================== Исключения потока управления ========================

struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal { Value value; };

// ======================== Окружение ========================

struct Env {
    std::map<std::string, Value> vars;
    Env* parent = nullptr;

    Env() = default;
    Env(Env* parent) : parent(parent) {}

    Value* lookup(const std::string& name){
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }

    bool set(const std::string& name, const Value& val){
        auto it = vars.find(name);
        if (it != vars.end()){
            it->second = val;
            return true;
        }
        if (parent) return parent->set(name, val);
        return false;
    }

    void define(const std::string& name, const Value& val){
        vars[name] = val;
    }
};

// ======================== Интерпретатор ========================

struct Interpreter {
    Env globalEnv;
    std::map<std::string, FuncDecl*> functions;
    std::map<std::string, StructDecl*> structs;

    // ---- Выражения ----

    Value evalExpr(Expr* expr, Env& env){
        if (!expr) return 0.0;

        if (auto *n = dynamic_cast<Number*>(expr)){
            return n->value;
        }

        if (auto *s = dynamic_cast<String*>(expr)){
            return s->value;
        }

        if (auto *b = dynamic_cast<Bool*>(expr)){
            return b->value;
        }

        if (auto *id = dynamic_cast<Identifier*>(expr)){
            Value* val = env.lookup(id->name);
            if (!val){
                std::cerr << "runtime error: неизвестная переменная '" << id->name << "'\n";
                std::exit(1);
            }
            return *val;
        }

        if (auto *bin = dynamic_cast<Binary*>(expr)){
            // присваивание
            if (bin->op == Operand::Equal){
                Value right = evalExpr(bin->right, env);
                assignValue(bin->left, right, env);
                return right;
            }

            Value left = evalExpr(bin->left, env);
            Value right = evalExpr(bin->right, env);

            switch (bin->op){
                case Operand::Add: {
                    if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)){
                        return valueToString(left) + valueToString(right);
                    }
                    return toNumber(left) + toNumber(right);
                }
                case Operand::Sub: return toNumber(left) - toNumber(right);
                case Operand::Mul: return toNumber(left) * toNumber(right);
                case Operand::Div: {
                    double r = toNumber(right);
                    if (r == 0.0){
                        std::cerr << "runtime error: деление на ноль\n";
                        std::exit(1);
                    }
                    return toNumber(left) / r;
                }
                case Operand::Mod: {
                    double r = toNumber(right);
                    if (r == 0.0){
                        std::cerr << "runtime error: деление на ноль\n";
                        std::exit(1);
                    }
                    return std::fmod(toNumber(left), r);
                }
                case Operand::EqualEqual: {
                    if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right))
                        return std::get<std::string>(left) == std::get<std::string>(right);
                    if (std::holds_alternative<bool>(left) && std::holds_alternative<bool>(right))
                        return std::get<bool>(left) == std::get<bool>(right);
                    return toNumber(left) == toNumber(right);
                }
                case Operand::NotEqual: {
                    if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right))
                        return std::get<std::string>(left) != std::get<std::string>(right);
                    if (std::holds_alternative<bool>(left) && std::holds_alternative<bool>(right))
                        return std::get<bool>(left) != std::get<bool>(right);
                    return toNumber(left) != toNumber(right);
                }
                case Operand::Less:         return toNumber(left) < toNumber(right);
                case Operand::Greater:      return toNumber(left) > toNumber(right);
                case Operand::LessEqual:    return toNumber(left) <= toNumber(right);
                case Operand::GreaterEqual: return toNumber(left) >= toNumber(right);
                case Operand::And:          return toBool(left) && toBool(right);
                case Operand::Or:           return toBool(left) || toBool(right);
                default: break;
            }
        }

        if (auto *un = dynamic_cast<Unary*>(expr)){
            if (un->op == Operand::Increment || un->op == Operand::Decrement){
                Value old = evalExpr(un->operand, env);
                double delta = (un->op == Operand::Increment) ? 1.0 : -1.0;
                Value updated = toNumber(old) + delta;
                assignValue(un->operand, updated, env);
                return old; // постфиксный — возвращает старое значение
            }
            Value operand = evalExpr(un->operand, env);
            if (un->op == Operand::UnaryMinus) return -toNumber(operand);
            if (un->op == Operand::Not) return !toBool(operand);
        }

        if (auto *call = dynamic_cast<FuncCall*>(expr)){
            // получаем имя функции
            auto *callee = dynamic_cast<Identifier*>(call->callee);
            if (!callee){
                std::cerr << "runtime error: вызов не-функции\n";
                std::exit(1);
            }

            std::string name = callee->name;

            // встроенные функции
            if (name == "print"){
                for (size_t i = 0; i < call->args.size(); i++){
                    if (i > 0) std::cout << " ";
                    std::cout << valueToString(evalExpr(call->args[i], env));
                }
                std::cout << "\n";
                return 0.0;
            }

            if (name == "input"){
                std::string line;
                std::getline(std::cin, line);
                return line;
            }

            if (name == "len"){
                if (call->args.size() != 1){
                    std::cerr << "runtime error: len() принимает 1 аргумент\n";
                    std::exit(1);
                }
                Value arg = evalExpr(call->args[0], env);
                if (auto *s = std::get_if<std::string>(&arg))
                    return (double)s->size();
                if (auto *a = std::get_if<std::vector<Value>>(&arg))
                    return (double)a->size();
                std::cerr << "runtime error: len() применим только к строкам и массивам\n";
                std::exit(1);
            }

            if (name == "exit"){
                int code = 0;
                if (!call->args.empty())
                    code = (int)toNumber(evalExpr(call->args[0], env));
                std::exit(code);
            }

            if (name == "panic"){
                std::string msg = "panic";
                if (!call->args.empty())
                    msg = valueToString(evalExpr(call->args[0], env));
                std::cerr << "runtime error: " << msg << "\n";
                std::exit(1);
            }

            // пользовательские функции
            auto it = functions.find(name);
            if (it == functions.end()){
                std::cerr << "runtime error: неизвестная функция '" << name << "'\n";
                std::exit(1);
            }

            FuncDecl* func = it->second;
            if (call->args.size() != func->params.size()){
                std::cerr << "runtime error: функция '" << name << "' ожидает "
                          << func->params.size() << " аргументов, получено "
                          << call->args.size() << "\n";
                std::exit(1);
            }

            Env funcEnv(&globalEnv);
            for (size_t i = 0; i < func->params.size(); i++){
                funcEnv.define(func->params[i].name, evalExpr(call->args[i], env));
            }

            try {
                execBlock(func->body, funcEnv);
            } catch (ReturnSignal& ret){
                return ret.value;
            }

            return 0.0;
        }

        if (auto *fa = dynamic_cast<FieldAccess*>(expr)){
            Value obj = evalExpr(fa->object, env);
            if (auto *m = std::get_if<std::map<std::string, Value>>(&obj)){
                auto it = m->find(fa->field);
                if (it == m->end()){
                    std::cerr << "runtime error: поле '" << fa->field << "' не найдено\n";
                    std::exit(1);
                }
                return it->second;
            }
            std::cerr << "runtime error: доступ к полю не-структуры\n";
            std::exit(1);
        }

        if (auto *aa = dynamic_cast<ArrayAccess*>(expr)){
            Value obj = evalExpr(aa->object, env);
            Value idx = evalExpr(aa->index, env);
            if (auto *arr = std::get_if<std::vector<Value>>(&obj)){
                int i = (int)toNumber(idx);
                if (i < 0 || i >= (int)arr->size()){
                    std::cerr << "runtime error: индекс " << i << " за границами массива (размер " << arr->size() << ")\n";
                    std::exit(1);
                }
                return (*arr)[i];
            }
            std::cerr << "runtime error: индексирование не-массива\n";
            std::exit(1);
        }

        if (auto *al = dynamic_cast<ArrayLiteral*>(expr)){
            std::vector<Value> elements;
            for (auto *elem : al->elements){
                elements.push_back(evalExpr(elem, env));
            }
            return elements;
        }

        if (auto *sl = dynamic_cast<StructLiteral*>(expr)){
            std::map<std::string, Value> fields;
            for (auto& fi : sl->fields){
                fields[fi.name] = evalExpr(fi.value, env);
            }
            return fields;
        }

        if (auto *ce = dynamic_cast<CastExpr*>(expr)){
            Value val = evalExpr(ce->value, env);
            if (ce->targetType == "int" || ce->targetType == "int64")
                return (double)(int64_t)toNumber(val);
            if (ce->targetType == "float" || ce->targetType == "float64")
                return toNumber(val);
            if (ce->targetType == "bool")
                return toBool(val);
            if (ce->targetType == "string")
                return valueToString(val);
            return val;
        }

        if (auto *na = dynamic_cast<NamespaceAccess*>(expr)){
            Value* val = globalEnv.lookup(na->nameSpace + "::" + na->member);
            if (!val){
                std::cerr << "runtime error: неизвестный элемент '" << na->nameSpace << "::" << na->member << "'\n";
                std::exit(1);
            }
            return *val;
        }

        std::cerr << "runtime error: неизвестный тип выражения\n";
        std::exit(1);
    }

    // ---- Присваивание в lvalue ----

    // Возвращает указатель на Value внутри окружения для lvalue
    Value* resolveLvalue(Expr* target, Env& env){
        if (auto *id = dynamic_cast<Identifier*>(target)){
            Value* val = env.lookup(id->name);
            if (!val){
                std::cerr << "runtime error: неизвестная переменная '" << id->name << "'\n";
                std::exit(1);
            }
            return val;
        }
        if (auto *aa = dynamic_cast<ArrayAccess*>(target)){
            Value* obj = resolveLvalue(aa->object, env);
            if (auto *arr = std::get_if<Array>(obj)){
                int i = (int)toNumber(evalExpr(aa->index, env));
                if (i < 0 || i >= (int)arr->size()){
                    std::cerr << "runtime error: индекс " << i << " за границами массива (размер " << arr->size() << ")\n";
                    std::exit(1);
                }
                return &(*arr)[i];
            }
            std::cerr << "runtime error: индексирование не-массива\n";
            std::exit(1);
        }
        if (auto *fa = dynamic_cast<FieldAccess*>(target)){
            Value* obj = resolveLvalue(fa->object, env);
            if (auto *m = std::get_if<Object>(obj)){
                auto it = m->find(fa->field);
                if (it == m->end()){
                    std::cerr << "runtime error: поле '" << fa->field << "' не найдено\n";
                    std::exit(1);
                }
                return &it->second;
            }
            std::cerr << "runtime error: доступ к полю не-структуры\n";
            std::exit(1);
        }
        std::cerr << "runtime error: присваивание в неподдерживаемый lvalue\n";
        std::exit(1);
    }

    void assignValue(Expr* target, const Value& val, Env& env){
        Value* lval = resolveLvalue(target, env);
        *lval = val;
    }

    // ---- Инструкции ----

    void execStmt(Stmt* stmt, Env& env){
        if (!stmt) return;

        if (auto *vd = dynamic_cast<VarDecl*>(stmt)){
            Value val = 0.0;
            if (vd->init) val = evalExpr(vd->init, env);
            env.define(vd->name, val);
            return;
        }

        if (auto *es = dynamic_cast<ExprStmt*>(stmt)){
            if (es->expr) evalExpr(es->expr, env);
            return;
        }

        if (auto *bl = dynamic_cast<Block*>(stmt)){
            execBlock(bl, env);
            return;
        }

        if (auto *ifn = dynamic_cast<If*>(stmt)){
            Value cond = evalExpr(ifn->condition, env);
            if (toBool(cond)){
                Env ifEnv(&env);
                execStmt(ifn->thenBranch, ifEnv);
            } else if (ifn->elseBranch){
                Env elseEnv(&env);
                execStmt(ifn->elseBranch, elseEnv);
            }
            return;
        }

        if (auto *wh = dynamic_cast<While*>(stmt)){
            while (true){
                Value cond = evalExpr(wh->condition, env);
                if (!toBool(cond)) break;
                try {
                    Env loopEnv(&env);
                    execStmt(wh->body, loopEnv);
                } catch (BreakSignal&){
                    break;
                } catch (ContinueSignal&){
                    continue;
                }
            }
            return;
        }

        if (dynamic_cast<Break*>(stmt)){
            throw BreakSignal{};
        }

        if (dynamic_cast<Continue*>(stmt)){
            throw ContinueSignal{};
        }

        if (auto *ret = dynamic_cast<Return*>(stmt)){
            Value val = 0.0;
            if (ret->value) val = evalExpr(ret->value, env);
            throw ReturnSignal{val};
        }
    }

    void execBlock(Block* block, Env& parentEnv){
        Env blockEnv(&parentEnv);
        for (auto *stmt : block->statements){
            execStmt(stmt, blockEnv);
        }
    }

    // ---- Объявления верхнего уровня ----

    void registerDecl(Stmt* decl){
        if (auto *fd = dynamic_cast<FuncDecl*>(decl)){
            functions[fd->name] = fd;
            return;
        }
        if (auto *sd = dynamic_cast<StructDecl*>(decl)){
            structs[sd->name] = sd;
            return;
        }
        if (auto *vd = dynamic_cast<VarDecl*>(decl)){
            Value val = 0.0;
            if (vd->init) val = evalExpr(vd->init, globalEnv);
            globalEnv.define(vd->name, val);
            return;
        }
        if (auto *nd = dynamic_cast<NamespaceDecl*>(decl)){
            for (auto *inner : nd->decls){
                if (auto *vd = dynamic_cast<VarDecl*>(inner)){
                    Value val = 0.0;
                    if (vd->init) val = evalExpr(vd->init, globalEnv);
                    globalEnv.define(nd->name + "::" + vd->name, val);
                }
                if (auto *fd = dynamic_cast<FuncDecl*>(inner)){
                    functions[nd->name + "::" + fd->name] = fd;
                }
            }
            return;
        }
        // TypeAlias — ничего не делаем в интерпретаторе
    }

    // ---- Запуск ----

    int run(const std::vector<Stmt*>& decls){
        // регистрируем все объявления
        for (auto *decl : decls){
            registerDecl(decl);
        }

        // ищем main
        auto it = functions.find("main");
        if (it == functions.end()){
            std::cerr << "runtime error: функция main не найдена\n";
            return 1;
        }

        FuncDecl* mainFunc = it->second;
        Env mainEnv(&globalEnv);

        try {
            execBlock(mainFunc->body, mainEnv);
        } catch (ReturnSignal& ret){
            return (int)toNumber(ret.value);
        }

        return 0;
    }
};

int interpret(const std::vector<Stmt*>& decls){
    Interpreter interp;
    return interp.run(decls);
}
