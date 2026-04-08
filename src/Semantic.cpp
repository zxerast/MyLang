#include "SymbolTable.hpp"
#include "Ast.hpp"
#include <expected>

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
    if (a->kind == TypeKind::Struct || a->kind == TypeKind::Alias)
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
        case TypeKind::Array:   return typeToString(t->elementType) + "[]";
        case TypeKind::Struct:  return t->name;
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

// --- Регистрация встроенных функций ---
//  Вызывается в начале analyze(), до анализа пользовательского кода
//  Каждая builtin-функция добавляется в глобальный scope как Symbol с kind=Function

void SemanticAnalyzer::registerBuiltins() {
    //  print — принимает один аргумент любого типа, возвращает void
    //  Особый случай: типы аргументов не проверяются (перегрузка встроенная)
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "print";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = makeType(TypeKind::Void);
        //  параметры не фиксируем — print принимает что угодно
        table.declare(sym);
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

    //  Пользовательский тип — ищем в таблице символов
    auto sym = table.resolve(name);
    if (sym) {
        if (sym->kind == SymbolKind::Struct) {
            auto t = makeType(TypeKind::Struct);
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
            errors.push_back("'" + id->name + "' is not declared");
            return nullptr;
        }
        //  Переменная объявлена но не инициализирована (int x; print(x);)
        if (!sym->isInitialized && sym->kind == SymbolKind::Variable) {
            errors.push_back("'" + id->name + "' is used before initialization");
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
                    errors.push_back("arithmetic operator requires numeric operands, got '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                {
                    //  Неявное widening: int32 + int64 → int64, int + float → float64
                    auto common = commonType(leftType, rightType);
                    if (!common) {
                        errors.push_back("incompatible types in arithmetic: '"
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
                    errors.push_back("comparison requires numeric operands, got '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (!commonType(leftType, rightType)) {
                    errors.push_back("incompatible types in comparison: '"
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Равенство (==, !=): типы должны совпадать (числа, строки, bool) → результат bool
            case Operand::EqualEqual:
            case Operand::NotEqual:
                if (!typesEqual(leftType, rightType)) {
                    errors.push_back("cannot compare '" + typeToString(leftType)
                        + "' with '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Логические (&&, ||): оба операнда bool → результат bool
            case Operand::And:
            case Operand::Or:
                if (leftType->kind != TypeKind::Bool || rightType->kind != TypeKind::Bool) {
                    errors.push_back("logical operator requires bool operands, got '"
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
                    errors.push_back("unary +/- requires numeric operand, got '"
                        + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            //  Логическое отрицание ! : операнд должен быть bool
            case Operand::Not:
                if (operandType->kind != TypeKind::Bool) {
                    errors.push_back("'!' requires bool operand, got '"
                        + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            //  Инкремент/декремент (++/--) : только целые числа
            case Operand::Increment:
            case Operand::Decrement:
                if (!isIntType(operandType)) {
                    errors.push_back("++/-- requires integer operand, got '"
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
        //  Пока поддерживаем только прямой вызов по имени (не через выражение)
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
            errors.push_back("'" + callee->name + "' is not declared");
            return nullptr;
        }
        if (sym->kind != SymbolKind::Function) {
            errors.push_back("'" + callee->name + "' is not a function");
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
                errors.push_back("'" + callee->name + "' expects "
                    + std::to_string(sym->funcInfo->params.size()) + " arguments, got "
                    + std::to_string(argTypes.size()));
            } else {
                //  Типы каждого аргумента
                for (size_t j = 0; j < argTypes.size(); j++) {
                    if (argTypes[j] && sym->funcInfo->params[j].second
                        && !typesEqual(argTypes[j], sym->funcInfo->params[j].second)) {
                        errors.push_back("argument " + std::to_string(j + 1) + " of '"
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

    //  ─── Доступ к полю структуры (p.x) ───
    //  Проверяем: объект — структура, поле существует
    if (auto* field = dynamic_cast<FieldAccess*>(expr)) {
        auto objType = analyzeExpr(field->object);
        if (!objType) return nullptr;

        if (objType->kind != TypeKind::Struct) {
            errors.push_back("field access on non-struct type '" + typeToString(objType) + "'");
            return nullptr;
        }

        //  Ищем определение структуры и проверяем наличие поля
        auto structSym = table.resolve(objType->name);
        if (structSym && structSym->structInfo) {
            for (auto& [fname, ftype] : structSym->structInfo->fields) {
                if (fname == field->field) {
                    expr->resolvedType = ftype;  //  Тип поля
                    return expr->resolvedType;
                }
            }
            errors.push_back("struct '" + objType->name + "' has no field '" + field->field + "'");
        }
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
            errors.push_back("index operator on non-array type '" + typeToString(objType) + "'");
            return nullptr;
        }

        //  Индекс должен быть целым
        if (indexType && !isIntType(indexType)) {
            errors.push_back("array index must be integer, got '" + typeToString(indexType) + "'");
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
            errors.push_back("cannot infer type of empty array literal");
            return nullptr;
        }

        //  Берём тип первого элемента как эталон
        auto firstType = analyzeExpr(arrLit->elements[0]);
        for (size_t j = 1; j < arrLit->elements.size(); j++) {
            auto elemType = analyzeExpr(arrLit->elements[j]);
            if (firstType && elemType && !typesEqual(firstType, elemType)) {
                errors.push_back("array element type mismatch: expected '"
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
            errors.push_back("'" + sLit->name + "' is not a struct type");
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
                            errors.push_back("field '" + init.name + "' of '" + sLit->name
                                + "': expected '" + typeToString(ftype)
                                + "', got '" + typeToString(valType) + "'");
                        }
                        break;
                    }
                }
                if (!found)
                    errors.push_back("struct '" + sLit->name + "' has no field '" + init.name + "'");
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
        analyzeExpr(cast->value);
        auto targetType = resolveTypeName(cast->targetType);
        if (!targetType) {
            errors.push_back("unknown type '" + cast->targetType + "' in cast");
            return nullptr;
        }
        expr->resolvedType = targetType;
        return expr->resolvedType;
    }

    //  ─── Доступ через namespace (Math::PI) ───
    if (auto* ns = dynamic_cast<NamespaceAccess*>(expr)) {
        if (!table.resolve(ns->nameSpace))
            errors.push_back("'" + ns->nameSpace + "' is not declared");
        return nullptr;  //  TODO: разрешение типа через namespace scope
    }

    return nullptr;
}

// --- Первый проход — сбор top-level имён ---
//  Обходим объявления верхнего уровня и регистрируем имена функций, структур,
//  алиасов и namespace в таблице символов ДО анализа тел.
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
                    errors.push_back("unknown parameter type '" + param.typeName
                        + "' in function '" + func->name + "'");
                sym->funcInfo->params.push_back({param.name, pType});
            }

            auto result = table.declare(sym);
            if (!result)
                errors.push_back(result.error());
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
                    errors.push_back("unknown field type '" + field.typeName
                        + "' in struct '" + structDecl->name + "'");
                sym->structInfo->fields.push_back({field.name, fType});
            }

            //  Тип самой структуры — Struct с именем
            auto t = makeType(TypeKind::Struct);
            t->name = structDecl->name;
            sym->type = t;

            auto result = table.declare(sym);
            if (!result)
                errors.push_back(result.error());
        }
        //  Type alias — регистрируем имя, разрешаем оригинальный тип
        else if (auto* alias = dynamic_cast<TypeAlias*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = alias->alias;
            sym->kind = SymbolKind::TypeAlias;
            sym->type = resolveTypeName(alias->original);
            if (!sym->type)
                errors.push_back("unknown type '" + alias->original
                    + "' in type alias '" + alias->alias + "'");

            auto result = table.declare(sym);
            if (!result)
                errors.push_back(result.error());
        }
        //  Namespace — регистрируем имя и рекурсивно собираем содержимое
        else if (auto* ns = dynamic_cast<NamespaceDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = ns->name;
            sym->kind = SymbolKind::Namespace;

            auto result = table.declare(sym);
            if (!result)
                errors.push_back(result.error());

            table.enterScope();
            collectTopLevel(ns->decls);
            table.exitScope();
        }
        //  Export — разворачиваем обёртку и собираем внутреннее объявление
        else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
            std::vector<Stmt*> inner = {exp->decl};
            collectTopLevel(inner);
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
        //  Имя уже зарегистрировано в collectTopLevel — здесь только анализ тела
        auto sym = table.resolve(func->name);

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
                    errors.push_back(pResult.error());
            }
        }
        //  Анализируем тело без лишнего enterScope — Block сам его создаст
        for (auto* s : func->body->statements)
            analyzeStmt(s);
        table.exitScope();
    }
    else if (auto* structDecl = dynamic_cast<StructDecl*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
        (void)structDecl;
    }
    else if (auto* alias = dynamic_cast<TypeAlias*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
        (void)alias;
    }
    else if (auto* ns = dynamic_cast<NamespaceDecl*>(stmt)) {
        //  Имя уже зарегистрировано в collectTopLevel, анализируем содержимое
        table.enterScope();
        collectTopLevel(ns->decls);  //  собираем имена внутри namespace
        for (auto* decl : ns->decls)
            analyzeStmt(decl);
        table.exitScope();
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(stmt)) {  //  export — просто анализируем обёрнутое объявление
        analyzeStmt(exp->decl);
    }
    //  Break, Continue — на этом этапе просто пропускаем
}

std::expected<void, std::string> SemanticAnalyzer::analyze(Program* program) {    //  Точка входа анализатора
    errors.clear();

    //  Регистрируем встроенные функции (print, len, input, exit, panic, push, pop)
    registerBuiltins();

    //  Первый проход — собираем все top-level имена (функции, структуры, алиасы)
    //  чтобы main мог вызывать функции, объявленные ниже
    collectTopLevel(program->decls);

    //  Второй проход — полный анализ тел функций и выражений
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
