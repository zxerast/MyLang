#include "SymbolTable.hpp"
#include "Ast.hpp"
#include <expected>
#include <fstream>
#include <sstream>
#include <filesystem>

// --- Вспомогательные функции для работы с типами ---

static std::shared_ptr<Type> makeType(TypeKind kind) {
    auto t = std::make_shared<Type>();
    t->kind = kind;
    return t;
}

static std::shared_ptr<Type> makeArrayType(std::shared_ptr<Type> elem, int size) {
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Array;
    t->elementType = elem;
    t->arraySize = size;
    return t;
}

static bool typesEqual(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return false;     
    if (a->kind != b->kind) return false;
    if (a->kind == TypeKind::Array)
        return typesEqual(a->elementType, b->elementType) && a->arraySize == b->arraySize;
    if (a->kind == TypeKind::DynArray)
        return typesEqual(a->elementType, b->elementType);
    if (a->kind == TypeKind::Struct || a->kind == TypeKind::Class || a->kind == TypeKind::Alias)
        return a->name == b->name;
    return true;  //  примитивы одного kind — одинаковые
}

static std::string typeToString(const std::shared_ptr<Type>& t) {
    if (!t) return "<unknown>";
    switch (t->kind) {
        case TypeKind::Int8:    return "int8";
        case TypeKind::Int16:   return "int16";
        case TypeKind::Int32:   return "int32";
        case TypeKind::Int64:   return "int64";
        case TypeKind::Uint8:   return "uint8";
        case TypeKind::Uint16:  return "uint16";
        case TypeKind::Uint32:  return "uint32";
        case TypeKind::Uint64:  return "uint64";
        case TypeKind::Float32: return "float32";
        case TypeKind::Float64: return "float64";
        case TypeKind::Bool:    return "bool";
        case TypeKind::Char:    return "char";
        case TypeKind::String:  return "string";
        case TypeKind::Void:    return "void";
        case TypeKind::Array:   return typeToString(t->elementType) + "[" + std::to_string(t->arraySize) + "]";
        case TypeKind::DynArray: return typeToString(t->elementType) + "[]";
        case TypeKind::Struct:  return t->name;
        case TypeKind::Class:   return t->name;
        case TypeKind::Alias:   return t->name;
        default:                return "<unknown>";
    }
}

static bool isIntType(const std::shared_ptr<Type>& t) {
    if (!t) return false;
    switch (t->kind) {
        case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
        case TypeKind::Uint8: case TypeKind::Uint16: case TypeKind::Uint32: case TypeKind::Uint64:
            return true;
        default:
            return false;
    }
}

static bool isFloatType(const std::shared_ptr<Type>& t) {
    if (!t) return false;
    return t->kind == TypeKind::Float32 || t->kind == TypeKind::Float64;
}

static bool isNumericType(const std::shared_ptr<Type>& t) {
    return isIntType(t) || isFloatType(t);
}

//  Ранг типа для widening-приведений
//  Чем больше ранг — тем "шире" тип
//  int8(1) < int16(2) < int32(3) < int64(4) < float32(5) < float64(6)
//  uint8(1) < uint16(2) < uint32(3) < uint64(4) < float32(5) < float64(6)
static int typeRank(const std::shared_ptr<Type>& t) {
    if (!t) return 0;
    switch (t->kind) {
        case TypeKind::Int8:    case TypeKind::Uint8:    return 1;
        case TypeKind::Int16:   case TypeKind::Uint16:   return 2;
        case TypeKind::Int32:   case TypeKind::Uint32:   return 3;
        case TypeKind::Int64:   case TypeKind::Uint64:   return 4;
        case TypeKind::Float32:                          return 5;
        case TypeKind::Float64:                          return 6;
        default:                                         return 0;
    }
}

//  Проверяет можно ли неявно привести тип from → to (widening)
//  Правила из спецификации:
//    int8 → int16 → int32 → int64
//    uint8 → uint16 → uint32 → uint64
//    float32 → float64
//    любой int/uint → float64
static bool isImplicitlyConvertible(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;
    if (typesEqual(from, to)) return true;  //  одинаковые типы — всегда ок

    //  Signed int → signed int (widening): int8 → int16 → int32 → int64
    if (from->kind >= TypeKind::Int8 && from->kind <= TypeKind::Int64
        && to->kind >= TypeKind::Int8 && to->kind <= TypeKind::Int64) {
        return typeRank(from) <= typeRank(to);
    }

    //  Unsigned int → unsigned int (widening): uint8 → uint16 → uint32 → uint64
    if (from->kind >= TypeKind::Uint8 && from->kind <= TypeKind::Uint64
        && to->kind >= TypeKind::Uint8 && to->kind <= TypeKind::Uint64) {
        return typeRank(from) <= typeRank(to);
    }

    //  float32 → float64
    if (from->kind == TypeKind::Float32 && to->kind == TypeKind::Float64)
        return true;

    //  Любой int/uint → float64
    if (isIntType(from) && to->kind == TypeKind::Float64)
        return true;

    //  Array (фиксированный литерал) → DynArray с совместимым типом элемента
    //  Позволяет: int[] arr = [1, 2, 3];  и  int[][] m = [[1, 2], [3, 4]];
    //  Рекурсивно проверяем элементы (для вложенных массивов)
    if (from->kind == TypeKind::Array && to->kind == TypeKind::DynArray)
        return from->elementType && to->elementType
            && isImplicitlyConvertible(from->elementType, to->elementType);

    return false;
}

//  Проверяет допустимость явного приведения cast<To>(from)
//  Таблица из спецификации: int↔int, int→float, float→int, float↔float,
//  int↔bool, char↔int.  string ↔ числовые — запрещено.
static bool isCastable(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;
    if (typesEqual(from, to)) return true;

    bool fromInt = isIntType(from);
    bool toInt = isIntType(to);
    bool fromFloat = isFloatType(from);
    bool toFloat = isFloatType(to);

    //  int ↔ int, int ↔ float, float ↔ float
    if ((fromInt || fromFloat) && (toInt || toFloat)) return true;
    //  int ↔ bool
    if (fromInt && to->kind == TypeKind::Bool) return true;
    if (from->kind == TypeKind::Bool && toInt) return true;
    //  char ↔ int
    if (from->kind == TypeKind::Char && toInt) return true;
    if (fromInt && to->kind == TypeKind::Char) return true;

    return false;
}

//  Определяет общий тип для бинарной операции (к чему привести оба операнда)
//  Например: int32 + int64 → int64, int + float → float64
//  Возвращает nullptr если типы несовместимы
static std::shared_ptr<Type> commonType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return nullptr;
    if (typesEqual(a, b)) return a;  //  одинаковые — ничего приводить не надо

    //  Оба signed int — берём больший
    if (a->kind >= TypeKind::Int8 && a->kind <= TypeKind::Int64
        && b->kind >= TypeKind::Int8 && b->kind <= TypeKind::Int64) {
        return typeRank(a) >= typeRank(b) ? a : b;
    }

    //  Оба unsigned int — берём больший
    if (a->kind >= TypeKind::Uint8 && a->kind <= TypeKind::Uint64
        && b->kind >= TypeKind::Uint8 && b->kind <= TypeKind::Uint64) {
        return typeRank(a) >= typeRank(b) ? a : b;
    }

    //  Оба float — берём больший
    if (isFloatType(a) && isFloatType(b))
        return typeRank(a) >= typeRank(b) ? a : b;

    //  int/uint + float → float64
    if (isIntType(a) && isFloatType(b)) return makeType(TypeKind::Float64);
    if (isFloatType(a) && isIntType(b)) return makeType(TypeKind::Float64);

    return nullptr;  //  несовместимы (например signed + unsigned)
}

//  Регистрация встроенных функций
//  Вызывается в начале analyze(), до анализа пользовательского кода
//  Каждая builtin-функция добавляется в глобальный scope как Symbol с kind=Function

void SemanticAnalyzer::registerBuiltins() {
    //  print — принимает один аргумент любого типа, возвращает void
    {
        auto sym = std::make_shared<Symbol>(); //  make_shared позволяет нескольким владельцам ссылаться на один символ из таблицы
        sym->name = "print";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);   //  print по изначальной задумке ничего не возвращает
        sym->funcInfo = std::make_shared<FuncInfo>();   //  Указатель на поля функции  
        sym->funcInfo->returnType = makeType(TypeKind::Void);   //  Какой тип на самом деле функция возвращает
        //  параметры не фиксируем — print принимает что угодно
        table.declare(sym); //  Все встроенные функции по умолчанию лежат в таблице
    }
    //  input — без аргументов, возвращает string (чтение строки из stdin)
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "input";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::String);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::String);
        table.declare(sym);
    }
    //  len — принимает массив или строку, возвращает int32
    //  Особый случай: тип аргумента проверяется отдельно (не через общий механизм)
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "len";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Int32);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Int32);
        table.declare(sym);
    }
    //  exit — принимает int (код возврата), возвращает void
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "exit";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Void);
        sym->funcInfo->params.push_back({"code", makeType(TypeKind::Int32)});
        table.declare(sym);
    }
    //  panic — принимает string (сообщение об ошибке), аварийно завершает программу
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "panic";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Void);
        sym->funcInfo->params.push_back({"msg", makeType(TypeKind::String)});
        table.declare(sym);
    }
    //  push — добавить элемент в конец динамического массива
    //  Особый случай: тип элемента зависит от типа массива
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "push";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Void);
        table.declare(sym);
    }
    //  pop — удалить и вернуть последний элемент массива
    //  Особый случай: тип возврата зависит от типа массива
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "pop";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Void);
        table.declare(sym);
    }
}

// --- Преобразование строки типа ("int", "float64", ...) в объект Type ---

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeName(const std::string& name) {
    //  Встроенные типы
    if (name == "int" || name == "int32")     return makeType(TypeKind::Int32);
    if (name == "int8")                       return makeType(TypeKind::Int8);
    if (name == "int16")                      return makeType(TypeKind::Int16);
    if (name == "int64")                      return makeType(TypeKind::Int64);
    if (name == "uint" || name == "uint32")   return makeType(TypeKind::Uint32);
    if (name == "uint8")                      return makeType(TypeKind::Uint8);
    if (name == "uint16")                     return makeType(TypeKind::Uint16);
    if (name == "uint64")                     return makeType(TypeKind::Uint64);
    if (name == "float" || name == "float64") return makeType(TypeKind::Float64);
    if (name == "float32")                    return makeType(TypeKind::Float32);
    if (name == "bool")                       return makeType(TypeKind::Bool);
    if (name == "char")                       return makeType(TypeKind::Char);
    if (name == "string")                     return makeType(TypeKind::String);
    if (name == "void")                       return makeType(TypeKind::Void);

    //  Массивный тип: суффикс [] или [N] на конце строки
    //  "int[]" → DynArray(int), "int[3]" → Array(int, 3)
    //  "int[][]" → DynArray(DynArray(int)), "int[3][4]" → Array(Array(int, 4), 3)
    //  Разбираем с конца: последний суффикс — внешний массив
    if (name.size() > 2 && name.back() == ']') {
        //  Ищем открывающую '[' последнего суффикса
        auto openBracket = name.rfind('[');
        if (openBracket == std::string::npos) return nullptr;

        //  Базовый тип — всё до последнего '['
        std::string baseName = name.substr(0, openBracket);
        //  Содержимое между [ и ] — пустое для [], число для [N]
        std::string inside = name.substr(openBracket + 1, name.size() - openBracket - 2);

        //  Рекурсивно разрешаем базовый тип (для int[][] сначала разрешит int[])
        auto elemType = resolveTypeName(baseName);
        if (!elemType) return nullptr;

        if (inside.empty()) {
            //  T[] — динамический массив
            auto t = std::make_shared<Type>();
            t->kind = TypeKind::DynArray;
            t->elementType = elemType;
            return t;
        } else {
            //  T[N] — фиксированный массив
            return makeArrayType(elemType, std::stoi(inside));
        }
    }

    //  Пользовательский тип — ищем в таблице символов
    auto sym = table.resolve(name);
    if (sym) {
        if (sym->kind == SymbolKind::Struct) {
            auto t = makeType(TypeKind::Struct);
            t->name = name;
            return t;
        }
        if (sym->kind == SymbolKind::Class) {
            auto t = makeType(TypeKind::Class);
            t->name = name;
            return t;
        }
        if (sym->kind == SymbolKind::TypeAlias) {
            return sym->type;  //  alias разрешается в оригинальный тип
        }
    }

    return nullptr;  //  Тип не найден
}

// --- Обход AST ---

std::shared_ptr<Type> SemanticAnalyzer::analyzeExpr(Expr* expr) {
    if (!expr) return nullptr;  //  В случае int x; без инициализатора — ничего не делаем

    //  ─── Литералы ───
    //  Числовой литерал: isFloat ставится парсером из SubType токена
    if (auto* num = dynamic_cast<Number*>(expr)) {
        if (num->isFloat)
            expr->resolvedType = makeType(TypeKind::Float64);   //  3.14 → float64
        else
            expr->resolvedType = makeType(TypeKind::Int32);     //  42   → int32
        return expr->resolvedType;
    }

    //  Строковый литерал → string
    if (dynamic_cast<String*>(expr)) {
        expr->resolvedType = makeType(TypeKind::String);
        return expr->resolvedType;
    }

    //  Булев литерал → bool
    if (dynamic_cast<Bool*>(expr)) {
        expr->resolvedType = makeType(TypeKind::Bool);
        return expr->resolvedType;
    }

    //  ─── Идентификатор ───
    //  Ищем имя в таблице символов, проверяем что переменная инициализирована
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        auto sym = table.resolve(id->name);
        if (!sym) {
            error(expr->line, "'" + id->name + "' is not declared");
            return nullptr;
        }
        //  Переменная объявлена но не инициализирована (int x; print(x);)
        if (!sym->isInitialized && sym->kind == SymbolKind::Variable) {
            error(expr->line, "'" + id->name + "' is used before initialization");
        }
        expr->resolvedType = sym->type;
        return expr->resolvedType;
    }

    //  ─── Бинарная операция ───
    //  Рекурсивно анализируем оба операнда, потом проверяем совместимость типов
    if (auto* bin = dynamic_cast<Binary*>(expr)) {
        auto leftType = analyzeExpr(bin->left);
        auto rightType = analyzeExpr(bin->right);

        //  Если хотя бы один операнд не определён — не можем проверить
        if (!leftType || !rightType) return nullptr;

        switch (bin->op) {
            //  Арифметика (+, -, *, /, %): оба операнда числовые и одного типа
            //  Исключение: string + string — конкатенация
            case Operand::Add:
                if (leftType->kind == TypeKind::String && rightType->kind == TypeKind::String) {
                    expr->resolvedType = makeType(TypeKind::String);  //  "Hello" + "World" → string
                    return expr->resolvedType;
                }
                [[fallthrough]];  //  иначе — обычная арифметика
            case Operand::Sub:    //  позднее могу чё угодно сюда добавить
            case Operand::Mul:
            case Operand::Div:
            case Operand::Mod:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, "arithmetic operator requires numeric operands, got '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                {
                    //  Неявное widening: int32 + int64 → int64, int + float → float64
                    auto common = commonType(leftType, rightType);
                    if (!common) {
                        error(expr->line, "incompatible types in arithmetic: '"
                            + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                        return nullptr;
                    }
                    expr->resolvedType = common;  //  Результат — общий (более широкий) тип
                }
                return expr->resolvedType;

            //  Сравнения (<, >, <=, >=): оба числовые, widening до общего типа → результат bool
            case Operand::Less:
            case Operand::Greater:
            case Operand::LessEqual:
            case Operand::GreaterEqual:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, "comparison requires numeric operands, got '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (!commonType(leftType, rightType)) {
                    error(expr->line, "incompatible types in comparison: '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Равенство (==, !=): типы должны совпадать (числа, строки, bool) → результат bool
            case Operand::EqualEqual:
            case Operand::NotEqual:
                if (!typesEqual(leftType, rightType)) {
                    error(expr->line, "cannot compare '" + typeToString(leftType)
                        + "' with '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Логические (&&, ||): оба операнда bool → результат bool
            case Operand::And:
            case Operand::Or:
                if (leftType->kind != TypeKind::Bool || rightType->kind != TypeKind::Bool) {
                    error(expr->line, "logical operator requires bool operands, got '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            default:
                return nullptr;
        }
    }

    //  ─── Унарная операция ───
    if (auto* un = dynamic_cast<Unary*>(expr)) {
        auto operandType = analyzeExpr(un->operand);
        if (!operandType) return nullptr;

        switch (un->op) {
            //  Унарный +/- : операнд должен быть числовым
            case Operand::UnaryMinus:
            case Operand::UnaryPlus:
                if (!isNumericType(operandType)) {
                    error(expr->line, "unary +/- requires numeric operand, got '"
                        + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            //  Логическое отрицание ! : операнд должен быть bool
            case Operand::Not:
                if (operandType->kind != TypeKind::Bool) {
                    error(expr->line, "'!' requires bool operand, got '"
                        + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Инкремент/декремент (++/--) : только целые числа
            case Operand::Increment:
            case Operand::Decrement:
                if (!isIntType(operandType)) {
                    error(expr->line, "++/-- requires integer operand, got '"
                        + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            default:
                return nullptr;
        }
    }

    //  ─── Вызов функции ───
    //  Проверяем: функция существует, количество аргументов, типы аргументов
    if (auto* call = dynamic_cast<FuncCall*>(expr)) {
        //  Вызов метода: obj.method(args)
        if (auto* fieldCallee = dynamic_cast<FieldAccess*>(call->callee)) {
            auto objType = analyzeExpr(fieldCallee->object);
            if (!objType || objType->kind != TypeKind::Class) {
                for (auto* arg : call->args) analyzeExpr(arg);
                if (objType)
                    error(expr->line, "method call on non-class type '" + typeToString(objType) + "'");
                return nullptr;
            }

            auto clsSym = table.resolve(objType->name);
            if (!clsSym || !clsSym->classInfo) return nullptr;

            auto mIt = clsSym->classInfo->methods.find(fieldCallee->field);
            if (mIt == clsSym->classInfo->methods.end()) {
                error(expr->line, "class '" + objType->name + "' has no method '" + fieldCallee->field + "'");
                for (auto* arg : call->args) analyzeExpr(arg);
                return nullptr;
            }

            auto& mInfo = mIt->second;
            std::vector<std::shared_ptr<Type>> argTypes;
            for (auto* arg : call->args)
                argTypes.push_back(analyzeExpr(arg));

            if (argTypes.size() != mInfo->params.size()) {
                error(expr->line, "method '" + fieldCallee->field + "' expects "
                    + std::to_string(mInfo->params.size()) + " arguments, got "
                    + std::to_string(argTypes.size()));
            } else {
                for (size_t j = 0; j < argTypes.size(); j++) {
                    if (argTypes[j] && mInfo->params[j].second
                        && !isImplicitlyConvertible(argTypes[j], mInfo->params[j].second)) {
                        error(expr->line, "method '" + fieldCallee->field + "' argument "
                            + std::to_string(j + 1) + ": cannot convert '"
                            + typeToString(argTypes[j]) + "' to '"
                            + typeToString(mInfo->params[j].second) + "'");
                    }
                }
            }

            expr->resolvedType = mInfo->returnType;
            return expr->resolvedType;
        }

        //  Прямой вызов по имени
        auto* callee = dynamic_cast<Identifier*>(call->callee);
        if (!callee) {
            analyzeExpr(call->callee);
            for (auto* arg : call->args)
                analyzeExpr(arg);
            return nullptr;
        }

        //  Ищем функцию в таблице символов
        auto sym = table.resolve(callee->name);
        if (!sym) {
            error(expr->line, "'" + callee->name + "' is not declared");
            return nullptr;
        }
        if (sym->kind != SymbolKind::Function) {
            error(expr->line, "'" + callee->name + "' is not a function");
            return nullptr;
        }

        //  Анализируем каждый аргумент и собираем их типы
        std::vector<std::shared_ptr<Type>> argTypes;
        for (auto* arg : call->args)
            argTypes.push_back(analyzeExpr(arg));

        //  Проверяем типы аргументов
        //  print и len — особые: print принимает что угодно, len принимает массив/строку
        if (sym->funcInfo && callee->name != "print" && callee->name != "len") {
            //  Количество аргументов
            if (argTypes.size() != sym->funcInfo->params.size()) {
                error(expr->line, "'" + callee->name + "' expects "
                    + std::to_string(sym->funcInfo->params.size()) + " arguments, got "
                    + std::to_string(argTypes.size()));
            } else {
                //  Типы каждого аргумента
                for (size_t j = 0; j < argTypes.size(); j++) {
                    if (argTypes[j] && sym->funcInfo->params[j].second
                        && !typesEqual(argTypes[j], sym->funcInfo->params[j].second)) {
                        error(expr->line, "argument " + std::to_string(j + 1) + " of '"
                            + callee->name + "': expected '"
                            + typeToString(sym->funcInfo->params[j].second)
                            + "', got '" + typeToString(argTypes[j]) + "'");
                    }
                }
            }
        }

        //  Возвращаем тип возврата функции
        expr->resolvedType = sym->funcInfo ? sym->funcInfo->returnType : sym->type;
        return expr->resolvedType;
    }

    //  ─── Доступ к полю структуры/класса (p.x) ───
    if (auto* field = dynamic_cast<FieldAccess*>(expr)) {
        auto objType = analyzeExpr(field->object);
        if (!objType) return nullptr;

        if (objType->kind == TypeKind::Struct) {
            auto structSym = table.resolve(objType->name);
            if (structSym && structSym->structInfo) {
                for (auto& [fname, ftype] : structSym->structInfo->fields) {
                    if (fname == field->field) {
                        expr->resolvedType = ftype;
                        return expr->resolvedType;
                    }
                }
                error(expr->line, "struct '" + objType->name + "' has no field '" + field->field + "'");
            }
            return nullptr;
        }

        if (objType->kind == TypeKind::Class) {
            auto clsSym = table.resolve(objType->name);
            if (clsSym && clsSym->classInfo) {
                //  Сначала ищем поле
                for (auto& [fname, ftype] : clsSym->classInfo->fields) {
                    if (fname == field->field) {
                        expr->resolvedType = ftype;
                        return expr->resolvedType;
                    }
                }
                //  Потом метод — возвращаем тип возврата метода (для obj.method без вызова — не поддерживаем,
                //  но нужно чтобы FuncCall мог увидеть что это метод)
                auto mIt = clsSym->classInfo->methods.find(field->field);
                if (mIt != clsSym->classInfo->methods.end()) {
                    expr->resolvedType = mIt->second->returnType;
                    return expr->resolvedType;
                }
                error(expr->line, "class '" + objType->name + "' has no field or method '" + field->field + "'");
            }
            return nullptr;
        }

        error(expr->line, "field access on non-struct/class type '" + typeToString(objType) + "'");
        return nullptr;
    }

    //  ─── Индексация массива (a[i]) ───
    //  Проверяем: объект — массив или строка, индекс — целое число
    if (auto* arr = dynamic_cast<ArrayAccess*>(expr)) {
        auto objType = analyzeExpr(arr->object);
        auto indexType = analyzeExpr(arr->index);
        if (!objType) return nullptr;

        //  Индексировать можно массивы и строки
        if (objType->kind != TypeKind::Array && objType->kind != TypeKind::DynArray
            && objType->kind != TypeKind::String) {
            error(expr->line, "index operator on non-array type '" + typeToString(objType) + "'");
            return nullptr;
        }

        //  Индекс должен быть целым
        if (indexType && !isIntType(indexType)) {
            error(expr->line, "array index must be integer, got '" + typeToString(indexType) + "'");
        }

        //  string[i] → char, array[i] → тип элемента
        if (objType->kind == TypeKind::String)
            expr->resolvedType = makeType(TypeKind::Char);
        else
            expr->resolvedType = objType->elementType;
        return expr->resolvedType;
    }

    //  ─── Литерал массива [1, 2, 3] ───
    //  Все элементы должны быть одного типа, результат — Array с этим типом
    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(expr)) {
        if (arrLit->elements.empty()) {
            error(expr->line, "cannot infer type of empty array literal");
            return nullptr;
        }

        //  Берём тип первого элемента как эталон
        auto firstType = analyzeExpr(arrLit->elements[0]);
        for (size_t j = 1; j < arrLit->elements.size(); j++) {
            auto elemType = analyzeExpr(arrLit->elements[j]);
            if (firstType && elemType && !typesEqual(firstType, elemType)) {
                error(expr->line, "array element type mismatch: expected '"
                    + typeToString(firstType) + "', got '" + typeToString(elemType) + "'");
            }
        }

        //  Размер массива = количество элементов в литерале
        expr->resolvedType = makeArrayType(firstType, static_cast<int>(arrLit->elements.size()));
        return expr->resolvedType;
    }

    //  ─── Литерал структуры Point { x: 5, y: 10 } ───
    //  Проверяем: структура существует, поля совпадают по именам и типам
    if (auto* sLit = dynamic_cast<StructLiteral*>(expr)) {
        auto sym = table.resolve(sLit->name);
        if (!sym || sym->kind != SymbolKind::Struct) {
            error(expr->line, "'" + sLit->name + "' is not a struct type");
            return nullptr;
        }

        //  Проверяем каждое инициализируемое поле
        if (sym->structInfo) {
            for (auto& init : sLit->fields) {
                auto valType = analyzeExpr(init.value);

                //  Ищем поле с таким именем в определении структуры
                bool found = false;
                for (auto& [fname, ftype] : sym->structInfo->fields) {
                    if (fname == init.name) {
                        found = true;
                        //  Проверяем совместимость типов
                        if (valType && ftype && !typesEqual(valType, ftype)) {
                            error(expr->line, "field '" + init.name + "' of '" + sLit->name
                                + "': expected '" + typeToString(ftype)
                                + "', got '" + typeToString(valType) + "'");
                        }
                        break;
                    }
                }
                if (!found)
                    error(expr->line, "struct '" + sLit->name + "' has no field '" + init.name + "'");
            }
        }

        //  Результат — тип этой структуры
        auto t = makeType(TypeKind::Struct);
        t->name = sLit->name;
        expr->resolvedType = t;
        return expr->resolvedType;
    }

    //  ─── Явное приведение типа cast<T>(expr) ───
    //  Анализируем внутреннее выражение, результат — целевой тип
    if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        auto fromType = analyzeExpr(cast->value);
        auto targetType = resolveTypeName(cast->targetType);
        if (!targetType) {
            error(expr->line, "unknown type '" + cast->targetType + "' in cast");
            return nullptr;
        }
        if (fromType && !isCastable(fromType, targetType)) {
            error(expr->line, "cannot cast '" + typeToString(fromType)
                + "' to '" + typeToString(targetType) + "'");
        }
        expr->resolvedType = targetType;
        return expr->resolvedType;
    }

    //  ─── Доступ через namespace (Math::PI) ───
    if (auto* ns = dynamic_cast<NamespaceAccess*>(expr)) {
        auto nsSym = table.resolve(ns->nameSpace);
        if (!nsSym) {
            error(expr->line, "'" + ns->nameSpace + "' is not declared");
            return nullptr;
        }
        if (nsSym->kind != SymbolKind::Namespace || !nsSym->nsScope) {
            error(expr->line, "'" + ns->nameSpace + "' is not a namespace");
            return nullptr;
        }
        //  Ищем member внутри scope namespace
        auto it = nsSym->nsScope->symbols.find(ns->member);
        if (it == nsSym->nsScope->symbols.end()) {
            error(expr->line, "'" + ns->member + "' is not declared in namespace '" + ns->nameSpace + "'");
            return nullptr;
        }
        expr->resolvedType = it->second->type;
        return expr->resolvedType;
    }

    //  ─── new ClassName(args...) ───
    if (auto* newExpr = dynamic_cast<NewExpr*>(expr)) {
        auto clsSym = table.resolve(newExpr->className);
        if (!clsSym || clsSym->kind != SymbolKind::Class) {
            error(expr->line, "'" + newExpr->className + "' is not a class");
            return nullptr;
        }

        //  Проверяем аргументы конструктора
        if (clsSym->classInfo && clsSym->classInfo->constructor) {
            auto& cParams = clsSym->classInfo->constructor->params;
            if (newExpr->args.size() != cParams.size()) {
                error(expr->line, "constructor of '" + newExpr->className + "' expects "
                    + std::to_string(cParams.size()) + " arguments, got "
                    + std::to_string(newExpr->args.size()));
            }
            for (size_t j = 0; j < newExpr->args.size(); j++) {
                auto argType = analyzeExpr(newExpr->args[j]);
                if (j < cParams.size() && argType && cParams[j].second) {
                    if (!isImplicitlyConvertible(argType, cParams[j].second))
                        error(expr->line, "constructor argument " + std::to_string(j + 1)
                            + ": cannot convert '" + typeToString(argType)
                            + "' to '" + typeToString(cParams[j].second) + "'");
                }
            }
        } else {
            //  Нет конструктора — аргументов быть не должно
            if (!newExpr->args.empty())
                error(expr->line, "class '" + newExpr->className + "' has no constructor");
            for (auto* arg : newExpr->args)
                analyzeExpr(arg);
        }

        auto t = makeType(TypeKind::Class);
        t->name = newExpr->className;
        expr->resolvedType = t;
        return expr->resolvedType;
    }

    return nullptr;
}

//  Проверяет, что инструкция (или блок) гарантированно завершается return
//  Используется для проверки non-void функций
static bool alwaysReturns(Stmt* stmt) {
    if (!stmt) return false;
    if (dynamic_cast<Return*>(stmt)) return true;

    //  Блок возвращает, если его последняя инструкция возвращает
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        if (block->statements.empty()) return false;
        return alwaysReturns(block->statements.back());
    }

    //  if/else возвращает, если обе ветки возвращают
    if (auto* ifStmt = dynamic_cast<If*>(stmt)) {
        if (!ifStmt->elseBranch) return false;
        return alwaysReturns(ifStmt->thenBranch) && alwaysReturns(ifStmt->elseBranch);
    }

    return false;
}

//  Первый проход — сбор top-level имён 
//  Обходим объявления верхнего уровня и регистрируем имена функций, структур,
//  алиасов и namespace в таблице символов до анализа их тел.
//  Это позволяет функции main вызывать функцию add, объявленную ниже.

void SemanticAnalyzer::collectTopLevel(const std::vector<Stmt*>& decls) {
    for (auto* decl : decls) {
        //  Функция — регистрируем имя, тип возврата и параметры
        if (auto* func = dynamic_cast<FuncDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = func->name;
            sym->kind = SymbolKind::Function;
            sym->type = resolveTypeName(func->returnType);

            //  Заполняем информацию о сигнатуре
            sym->funcInfo = std::make_shared<FuncInfo>();
            sym->funcInfo->returnType = sym->type;
            for (auto& param : func->params) {
                auto pType = resolveTypeName(param.typeName);
                if (!pType)
                    error(decl->line, "unknown parameter type '" + param.typeName
                        + "' in function '" + func->name + "'");
                sym->funcInfo->params.push_back({param.name, pType});
            }

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());
        }
        //  Структура — регистрируем имя и информацию о полях
        else if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = structDecl->name;
            sym->kind = SymbolKind::Struct;

            //  Заполняем информацию о полях с их типами
            sym->structInfo = std::make_shared<StructInfo>();
            sym->structInfo->name = structDecl->name;
            for (auto& field : structDecl->fields) {
                auto fType = resolveTypeName(field.typeName);
                if (!fType)
                    error(decl->line, "unknown field type '" + field.typeName
                        + "' in struct '" + structDecl->name + "'");
                sym->structInfo->fields.push_back({field.name, fType});
            }

            //  Тип самой структуры — Struct с именем
            auto t = makeType(TypeKind::Struct);
            t->name = structDecl->name;
            sym->type = t;

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());
        }
        //  Type alias — регистрируем имя, разрешаем оригинальный тип
        else if (auto* alias = dynamic_cast<TypeAlias*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = alias->alias;
            sym->kind = SymbolKind::TypeAlias;
            sym->type = resolveTypeName(alias->original);
            if (!sym->type)
                error(decl->line, "unknown type '" + alias->original
                    + "' in type alias '" + alias->alias + "'");

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());
        }
        //  Namespace — регистрируем имя и рекурсивно собираем содержимое
        else if (auto* ns = dynamic_cast<NamespaceDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = ns->name;
            sym->kind = SymbolKind::Namespace;

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());

            table.enterScope();
            collectTopLevel(ns->decls);
            sym->nsScope = table.currentScope();
            table.exitScope();
        }
        //  Класс — регистрируем имя, поля, методы, конструктор, деструктор
        else if (auto* cls = dynamic_cast<ClassDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = cls->name;
            sym->kind = SymbolKind::Class;

            sym->classInfo = std::make_shared<ClassInfo>();
            sym->classInfo->name = cls->name;

            //  Поля
            for (auto& field : cls->fields) {
                auto fType = resolveTypeName(field.typeName);
                if (!fType)
                    error(decl->line, "unknown field type '" + field.typeName
                        + "' in class '" + cls->name + "'");
                sym->classInfo->fields.push_back({field.name, fType});
            }

            //  Методы
            for (auto* method : cls->methods) {
                auto mInfo = std::make_shared<FuncInfo>();
                mInfo->returnType = resolveTypeName(method->returnType);
                for (auto& param : method->params) {
                    auto pType = resolveTypeName(param.typeName);
                    mInfo->params.push_back({param.name, pType});
                }
                sym->classInfo->methods[method->name] = mInfo;
            }

            //  Конструктор
            if (cls->constructor) {
                auto cInfo = std::make_shared<FuncInfo>();
                cInfo->returnType = makeType(TypeKind::Void);
                for (auto& param : cls->constructor->params) {
                    auto pType = resolveTypeName(param.typeName);
                    cInfo->params.push_back({param.name, pType});
                }
                sym->classInfo->constructor = cInfo;
            }

            //  Деструктор
            sym->classInfo->hasDestructor = (cls->destructor != nullptr);

            auto t = makeType(TypeKind::Class);
            t->name = cls->name;
            sym->type = t;

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());
        }
        //  Export — разворачиваем обёртку и собираем внутреннее объявление, помечаем isExported
        else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
            std::vector<Stmt*> inner = {exp->decl};
            collectTopLevel(inner);

            //  Определяем имя объявленного символа и помечаем его exported
            std::string exportedName;
            if (auto* f = dynamic_cast<FuncDecl*>(exp->decl))       exportedName = f->name;
            else if (auto* s = dynamic_cast<StructDecl*>(exp->decl)) exportedName = s->name;
            else if (auto* c = dynamic_cast<ClassDecl*>(exp->decl))  exportedName = c->name;
            else if (auto* a = dynamic_cast<TypeAlias*>(exp->decl))  exportedName = a->alias;
            else if (auto* v = dynamic_cast<VarDecl*>(exp->decl))    exportedName = v->name;
            else if (auto* n = dynamic_cast<NamespaceDecl*>(exp->decl)) exportedName = n->name;

            if (!exportedName.empty()) {
                auto sym = table.resolve(exportedName);
                if (sym) sym->isExported = true;
            }
        }
        //  VarDecl на верхнем уровне — пропускаем, обработается во втором проходе
    }
}

void SemanticAnalyzer::analyzeBlock(Block* block) {
    table.enterScope();
    for (auto* s : block->statements)   //  Смотрим стэйтменты внутри текущей области
        analyzeStmt(s);
    table.exitScope();
}

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (auto* var = dynamic_cast<VarDecl*>(stmt)) { //  Если это объявление переменной
        auto sym = std::make_shared<Symbol>();
        sym->name = var->name;
        sym->kind = SymbolKind::Variable;
        sym->isConst = var->isConst;
        sym->isInitialized = (var->init != nullptr);

        //  Анализируем инициализатор и получаем его тип
        std::shared_ptr<Type> initType = analyzeExpr(var->init);

        if (var->isAuto) {
            //  auto — выводим тип из инициализатора
            if (!initType)
                error(stmt->line, "'auto' requires initializer to infer type for '" + var->name + "'");
            sym->type = initType;
        } else {
            //  Явный тип — разрешаем имя типа
            sym->type = resolveTypeName(var->typeName);
            if (!sym->type)
                error(stmt->line, "unknown type '" + var->typeName + "'");
            else if (sym->type->kind == TypeKind::Void)
                error(stmt->line, "cannot declare variable '" + var->name + "' with type 'void'");

            //  Проверяем совместимость типа переменной и инициализатора
            //  Допускается widening: int32 x = int8_val — ок
            if (initType && sym->type) {
                if (!isImplicitlyConvertible(initType, sym->type)) {
                    error(stmt->line, "cannot initialize '" + var->name + "' of type '"
                        + typeToString(sym->type) + "' with '" + typeToString(initType) + "'");
                }
            }
        }

        auto result = table.declare(sym);   //  Сохраняем в таблицу символов
        if (!result)
            error(stmt->line, result.error());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {    //  Если это блок то соответственно анализируем блок
        analyzeBlock(block);
    }
    else if (auto* assign = dynamic_cast<Assign*>(stmt)) {  //  Присваивание: проверяем типы, const, инициализацию
        auto targetType = analyzeExpr(assign->target);
        auto valueType = analyzeExpr(assign->value);

        //  Проверяем что target не const
        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            auto sym = table.resolve(id->name);
            if (sym && sym->isConst)
                error(stmt->line, "cannot assign to const variable '" + id->name + "'");
            //  Помечаем переменную как инициализированную (для int x; x = 5;)
            if (sym) sym->isInitialized = true;
        }

        //  Проверяем совместимость типов (с учётом widening)
        if (targetType && valueType) {
            if (!isImplicitlyConvertible(valueType, targetType)) {
                error(stmt->line, "type mismatch in assignment: cannot assign '"
                    + typeToString(valueType) + "' to '" + typeToString(targetType) + "'");
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<If*>(stmt)) {  //  if — условие должно быть bool
        auto condType = analyzeExpr(ifStmt->condition);
        if (condType && condType->kind != TypeKind::Bool)
            error(stmt->line, "if condition must be bool, got '" + typeToString(condType) + "'");
        analyzeStmt(ifStmt->thenBranch);
        if (ifStmt->elseBranch)
            analyzeStmt(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<While*>(stmt)) {  //  while — условие должно быть bool
        auto condType = analyzeExpr(whileStmt->condition);
        if (condType && condType->kind != TypeKind::Bool)
            error(stmt->line, "while condition must be bool, got '" + typeToString(condType) + "'");
        loopDepth++;
        analyzeStmt(whileStmt->body);
        loopDepth--;
    }
    else if (auto* breakStmt = dynamic_cast<Break*>(stmt)) {
        if (loopDepth == 0)
            error(stmt->line, "'break' outside of loop");
    }
    else if (auto* continueStmt = dynamic_cast<Continue*>(stmt)) {
        if (loopDepth == 0)
            error(stmt->line, "'continue' outside of loop");
    }
    else if (auto* ret = dynamic_cast<Return*>(stmt)) {  //  return — проверяем тип возвращаемого значения
        if (ret->value) {
            auto valType = analyzeExpr(ret->value);
            //  Сравниваем с типом возврата текущей функции
            if (valType && currentReturnType) {
                if (!isImplicitlyConvertible(valType, currentReturnType)) {
                    error(stmt->line, "return type mismatch: expected '"
                        + typeToString(currentReturnType) + "', got '" + typeToString(valType) + "'");
                }
            }
        } else {
            //  return; без значения — функция должна быть void
            if (currentReturnType && currentReturnType->kind != TypeKind::Void)
                error(stmt->line, "return without value in non-void function");
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {  //  выражение как инструкция (напр. print(x);)
        analyzeExpr(exprStmt->expr);
    }
    else if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {  //  Объявление функции
        //  Имя уже зарегистрировано в collectTopLevel — здесь только анализ тела
        auto sym = table.resolve(func->name);

        //  Сохраняем предыдущий тип возврата и ставим текущий (для проверки return)
        auto prevReturnType = currentReturnType;
        currentReturnType = (sym && sym->funcInfo) ? sym->funcInfo->returnType : nullptr;

        //  Тело функции — новый scope, в который добавляем параметры с типами
        table.enterScope();
        if (sym && sym->funcInfo) {
            for (size_t j = 0; j < func->params.size(); j++) {
                auto pSym = std::make_shared<Symbol>();
                pSym->name = func->params[j].name;
                pSym->kind = SymbolKind::Variable;
                pSym->isInitialized = true;  //  параметры всегда инициализированы
                //  Берём тип из funcInfo, собранного в collectTopLevel
                pSym->type = (j < sym->funcInfo->params.size()) ? sym->funcInfo->params[j].second : nullptr;

                auto pResult = table.declare(pSym);
                if (!pResult)
                    error(stmt->line, pResult.error());
            }
        }
        //  Анализируем тело без лишнего enterScope — Block сам его создаст
        for (auto* s : func->body->statements)
            analyzeStmt(s);
        table.exitScope();

        //  Проверяем что non-void функция возвращает значение на всех путях
        if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
            if (!alwaysReturns(func->body))
                error(stmt->line, "function '" + func->name + "' does not return a value on all paths");
        }

        //  Восстанавливаем тип возврата внешней функции (для вложенных объявлений)
        currentReturnType = prevReturnType;
    }
    else if (auto* structDecl = dynamic_cast<StructDecl*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
        (void)structDecl;
    }
    else if (auto* alias = dynamic_cast<TypeAlias*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
        (void)alias;
    }
    else if (auto* cls = dynamic_cast<ClassDecl*>(stmt)) {
        auto clsSym = table.resolve(cls->name);
        if (!clsSym || !clsSym->classInfo) return;

        //  Тип self для методов
        auto selfType = makeType(TypeKind::Class);
        selfType->name = cls->name;

        //  Анализируем тела методов
        for (auto* method : cls->methods) {
            auto prevRetType = currentReturnType;
            auto mIt = clsSym->classInfo->methods.find(method->name);
            currentReturnType = (mIt != clsSym->classInfo->methods.end()) ? mIt->second->returnType : nullptr;

            table.enterScope();
            //  Добавляем self
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            //  Добавляем параметры
            if (mIt != clsSym->classInfo->methods.end()) {
                for (size_t j = 0; j < method->params.size(); j++) {
                    auto pSym = std::make_shared<Symbol>();
                    pSym->name = method->params[j].name;
                    pSym->kind = SymbolKind::Variable;
                    pSym->isInitialized = true;
                    pSym->type = (j < mIt->second->params.size()) ? mIt->second->params[j].second : nullptr;
                    table.declare(pSym);
                }
            }

            for (auto* s : method->body->statements)
                analyzeStmt(s);
            table.exitScope();

            if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
                if (!alwaysReturns(method->body))
                    error(stmt->line, "method '" + cls->name + "." + method->name + "' does not return a value on all paths");
            }
            currentReturnType = prevRetType;
        }

        //  Анализируем тело конструктора
        if (cls->constructor) {
            auto prevRetType = currentReturnType;
            currentReturnType = makeType(TypeKind::Void);

            table.enterScope();
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            if (clsSym->classInfo->constructor) {
                for (size_t j = 0; j < cls->constructor->params.size(); j++) {
                    auto pSym = std::make_shared<Symbol>();
                    pSym->name = cls->constructor->params[j].name;
                    pSym->kind = SymbolKind::Variable;
                    pSym->isInitialized = true;
                    pSym->type = (j < clsSym->classInfo->constructor->params.size())
                        ? clsSym->classInfo->constructor->params[j].second : nullptr;
                    table.declare(pSym);
                }
            }

            for (auto* s : cls->constructor->body->statements)
                analyzeStmt(s);
            table.exitScope();
            currentReturnType = prevRetType;
        }

        //  Анализируем тело деструктора
        if (cls->destructor) {
            auto prevRetType = currentReturnType;
            currentReturnType = makeType(TypeKind::Void);

            table.enterScope();
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            for (auto* s : cls->destructor->body->statements)
                analyzeStmt(s);
            table.exitScope();
            currentReturnType = prevRetType;
        }
    }
    else if (auto* del = dynamic_cast<DeleteStmt*>(stmt)) {
        auto valType = analyzeExpr(del->value);
        if (valType && valType->kind != TypeKind::Class)
            error(stmt->line, "delete requires a class instance, got '" + typeToString(valType) + "'");
    }
    else if (auto* ns = dynamic_cast<NamespaceDecl*>(stmt)) {
        //  Имя уже зарегистрировано в collectTopLevel, scope сохранён в nsScope
        //  Входим в сохранённый scope и анализируем тела объявлений
        auto nsSym = table.resolve(ns->name);
        if (nsSym && nsSym->nsScope) {
            table.pushScope(nsSym->nsScope);
            for (auto* decl : ns->decls)
                analyzeStmt(decl);
            table.exitScope();
        }
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(stmt)) {  //  export — просто анализируем обёрнутое объявление
        analyzeStmt(exp->decl);
    }
    //  Break, Continue — на этом этапе просто пропускаем
}

void SemanticAnalyzer::processImport(ImportDecl* imp) {
    //  Разрешаем путь относительно текущего файла
    std::filesystem::path base = std::filesystem::path(currentFilePath).parent_path();
    std::filesystem::path target = base / imp->path;

    if (!std::filesystem::exists(target)) {
        error(imp->line, "cannot open imported file '" + imp->path + "'");
        return;
    }

    std::string absPath = std::filesystem::canonical(target).string();

    //  Защита от циклических импортов
    if (importedFiles.contains(absPath)) return;
    importedFiles.insert(absPath);

    //  Читаем файл
    std::ifstream file(absPath);
    if (!file.is_open()) {
        error(imp->line, "cannot open imported file '" + imp->path + "'");
        return;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();

    //  Токенизируем
    auto tokens = tokenize(source);
    if (!tokens) {
        error(imp->line, "in '" + imp->path + "': " + tokens.error());
        return;
    }

    //  Парсим
    auto nodes = parse(*tokens);
    if (!nodes) {
        error(imp->line, "in '" + imp->path + "': " + nodes.error());
        return;
    }

    //  Сохраняем контекст и переключаемся на импортируемый файл
    std::string prevFile = currentFilePath;
    currentFilePath = absPath;

    //  Собираем top-level объявления импортируемого файла
    collectTopLevel(*nodes);

    //  Полный анализ тел (чтобы ошибки внутри модуля тоже обнаруживались)
    for (auto* decl : *nodes)
        analyzeStmt(decl);

    currentFilePath = prevFile;

    //  Удаляем из таблицы символов всё, что не помечено export
    //  (оставляем только exported символы доступными вызывающему модулю)
    auto scope = table.currentScope();
    std::vector<std::string> toRemove;
    for (auto& [name, sym] : scope->symbols) {
        if (!sym->isExported && sym->kind != SymbolKind::Function) {
            //  Встроенные функции (print, len...) не удаляем
            toRemove.push_back(name);
        }
        //  Пользовательские функции без export тоже удаляем
        if (sym->kind == SymbolKind::Function && !sym->isExported) {
            //  Проверяем что это не builtin (builtins не имеют line)
            bool isBuiltin = (name == "print" || name == "len" || name == "input"
                           || name == "exit" || name == "panic" || name == "push" || name == "pop");
            if (!isBuiltin)
                toRemove.push_back(name);
        }
    }
    for (auto& name : toRemove)
        scope->symbols.erase(name);
}

std::expected<void, std::string> SemanticAnalyzer::analyze(Program* program, const std::string& filePath) {    //  Точка входа анализатора
    errors.clear(); //  Очищаем массив ошибок
    currentFilePath = std::filesystem::canonical(filePath).string();    
    // Canonical гарантия что не будет повторных импортов одного и того же файла по разным путям
    importedFiles.insert(currentFilePath);
    // Множество всех импортируемых файлов

    //  Регистрируем встроенные функции (print, len, input, exit, panic, push, pop)
    registerBuiltins();

    //  Обрабатываем импорты — загружаем exported символы из других файлов
    for (auto* decl : program->decls) {
        if (auto* imp = dynamic_cast<ImportDecl*>(decl))
            processImport(imp);
    }

    //  Первый проход — собираем все top-level имена (функции, структуры, алиасы)
    //  чтобы main мог вызывать функции, объявленные ниже
    collectTopLevel(program->decls);

    //  Второй проход — полный анализ тел функций и выражений
    for (auto* decl : program->decls) {
        if (dynamic_cast<ImportDecl*>(decl)) continue;  //  Импорты уже обработаны
        analyzeStmt(decl);
    }

    //  Проверяем наличие точки входа: int main()
    auto mainSym = table.resolve("main");
    if (!mainSym || mainSym->kind != SymbolKind::Function) {
        errors.push_back("missing entry point: expected function 'int main()'");
    } 
    
    else if (!mainSym->funcInfo) {
        errors.push_back("'main' is not a function");
    } 
    
    else {
        if (!mainSym->funcInfo->returnType || mainSym->funcInfo->returnType->kind != TypeKind::Int32)
            errors.push_back("'main' must return 'int', got '" + typeToString(mainSym->funcInfo->returnType) + "'");
        if (!mainSym->funcInfo->params.empty())
            errors.push_back("'main' must take no parameters");
    }

    if (!errors.empty()) {
        std::string msg;
        for (auto& e : errors)
            msg += e + "\n";
        return std::unexpected(msg);
    }
    return {};
}
