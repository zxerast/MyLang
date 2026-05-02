#include "SymbolTable.hpp"
#include "Ast.hpp"
#include <expected>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <limits>
#include <unordered_set>
#include <clang-c/Index.h>

// функции для работы с типами

static std::shared_ptr<Type> makeType(TypeKind kind) {  //  Определение сущности типа
    auto type = std::make_shared<Type>();
    type->kind = kind;
    return type;
}

static std::shared_ptr<Type> makeArrayType(std::shared_ptr<Type> elem, int size) {  // Тоже самое для массивов
    auto type = std::make_shared<Type>();
    type->kind = TypeKind::Array;
    type->elementType = elem;  //  Тип массива определяем по первому элементу
    type->arraySize = size;
    return type;
}

static std::shared_ptr<Type> makeDynArrayType(std::shared_ptr<Type> elem) {
    auto type = std::make_shared<Type>();
    type->kind = TypeKind::DynArray;
    type->elementType = elem;
    type->arraySize = -1;
    return type;
}

static bool typesEqual(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) {     //  Кого-то из типов несуществует
        return false;
    }     
    if (a->kind != b->kind) {   //  Если типы объекты разные по сущности (kind из Type.hpp)
        return false;
    }
    if (a->kind == TypeKind::Array) {   //  Массивы рекурсивно проверяем по типам элементов
        if (!typesEqual(a->elementType, b->elementType)) {
            return false;
        }

        return a->arraySize == b->arraySize;
    }
    if (a->kind == TypeKind::DynArray) {    //  Динамические точно также, но можно без размера
        return typesEqual(a->elementType, b->elementType);
    }
    if (a->kind == TypeKind::Struct || a->kind == TypeKind::Class || a->kind == TypeKind::Alias) {
        return a->name == b->name;  //  Структуры, классы и алаясы просто сверяем по имени
    }
    return true;  //  примитивы одной сущности — одинаковые
}

static std::string typeToString(const std::shared_ptr<Type>& type) {   //  Переводим тип в строку
    if (!type) return "<unknown>";                                     //  Это для записи в ошибки
    switch (type->kind) {
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
        case TypeKind::Array:
            return typeToString(type->elementType) + "[" + std::to_string(type->arraySize) + "]";
        
        case TypeKind::Struct:  return type->name;
        case TypeKind::Class:   return type->name;
        case TypeKind::Alias:   return type->name;
        case TypeKind::Null:    return "null";
        default:                return "<unknown>";
    }
}

static std::string typeNameToString(TypeName* typeName) {
    if (!typeName) {
        return "<auto>";
    }

    std::string result = typeName->base;

    for (const auto& suffix : typeName->suffixes) {
        if (suffix.isDynamic) {
            result += "[]";
        }
        else {
            result += "[";
            result += "<expr>";
            result += "]";
        }
    }

    return result;
}

static bool isIntType(const std::shared_ptr<Type>& type) {  //  Целочисленный ли тип?
    if (!type) return false;

    switch (type->kind) {
        case TypeKind::Int8: 
        case TypeKind::Int16: 
        case TypeKind::Int32:
        case TypeKind::Int64:
        case TypeKind::Uint8: 
        case TypeKind::Uint16: 
        case TypeKind::Uint32: 
        case TypeKind::Uint64:
            return true;
        default:
            return false;
    }
}

static bool isFloatType(const std::shared_ptr<Type>& type) {    //  Дробный ли тип?
    if (!type) return false;

    return type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64;
}

static bool isNumericType(const std::shared_ptr<Type>& type) {  //  Числовой ли тип?
    return isIntType(type) || isFloatType(type);    
}

static bool isNegativeIntegerLiteralExpr(Expr* expr) {
    auto* unary = dynamic_cast<Unary*>(expr);
    if (!unary || unary->op != Operand::UnaryMinus) {
        return false;
    }

    auto* number = dynamic_cast<Number*>(unary->operand);
    return number && !number->isFloat;
}

static bool isInputSupportedType(const std::shared_ptr<Type>& type) {
    if (!type) return false;

    if (type->kind == TypeKind::String || type->kind == TypeKind::Char || isIntType(type) || isFloatType(type)) {
        return true;
    }

    if (type->kind == TypeKind::Array) {
        return type->elementType && (type->elementType->kind == TypeKind::String
                || type->elementType->kind == TypeKind::Char
                || isIntType(type->elementType)
                || isFloatType(type->elementType));
    }

    if (type->kind == TypeKind::DynArray) {
        return type->elementType && (type->elementType->kind == TypeKind::String
                || type->elementType->kind == TypeKind::Char
                || isIntType(type->elementType)
                || isFloatType(type->elementType));
    }

    return false;
}

//  Ранг типа для widening-приведений
//  Любой тип меньшего ранга мы можем неявно преобразовать в тип большего, но не наоборот
//  int8(1) < int16(2) < int32(3) < int64(4) < float32(5) < float64(6)
//  uint8(1) < uint16(2) < uint32(3) < uint64(4) < float32(5) < float64(6)
static int typeRank(const std::shared_ptr<Type>& type) {
    if (!type) return 0;
    switch (type->kind) {
        case TypeKind::Int8:    
        case TypeKind::Uint8:    return 1;
        case TypeKind::Int16:   
        case TypeKind::Uint16:   return 2;
        case TypeKind::Int32:   
        case TypeKind::Uint32:   return 3;
        case TypeKind::Int64:   
        case TypeKind::Uint64:   return 4;
        case TypeKind::Float32:  return 5;
        case TypeKind::Float64:  return 6;
        default:                 return 0;
    }
}

static bool isSignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Int8 && type->kind <= TypeKind::Int64;
}

static bool isUnsignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Uint8 && type->kind <= TypeKind::Uint64;
}

static int intBitWidth(const std::shared_ptr<Type>& type) {
    if (!type) return 0;
    switch (type->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8: return 8;
        case TypeKind::Int16:
        case TypeKind::Uint16: return 16;
        case TypeKind::Int32:
        case TypeKind::Uint32: return 32;
        case TypeKind::Int64:
        case TypeKind::Uint64: return 64;
        default: return 0;
    }
}

static TypeKind signedIntKindForBits(int bits) {
    if (bits <= 8) return TypeKind::Int8;
    if (bits <= 16) return TypeKind::Int16;
    if (bits <= 32) return TypeKind::Int32;
    return TypeKind::Int64;
}

static bool isImplicitlyConvertible(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;

    if (typesEqual(from, to)) return true;  //  одинаковые типы — всегда ок

    //  Signed int -> signed int 
    if (from->kind >= TypeKind::Int8 && from->kind <= TypeKind::Int64 && to->kind >= TypeKind::Int8 && to->kind <= TypeKind::Int64) {
        return typeRank(from) <= typeRank(to);
    }

    //  Unsigned int -> unsigned int 
    if (from->kind >= TypeKind::Uint8 && from->kind <= TypeKind::Uint64 && to->kind >= TypeKind::Uint8 && to->kind <= TypeKind::Uint64) {
        return typeRank(from) <= typeRank(to);
    }

    //  float32 -> float64
    if (from->kind == TypeKind::Float32 && to->kind == TypeKind::Float64)
        return true;

    //  Любой int/uint → float32/float64
    if (isIntType(from) && isFloatType(to))
        return true;

    //  Array -> DynArray
    //  Позволяет: int[] arr = [1, 2, 3];  и  int[][] m = [[1, 2], [3, 4]];
    //  Рекурсивно проверяем элементы (для вложенных массивов)
    if (from->kind == TypeKind::Array && to->kind == TypeKind::DynArray)
        return from->elementType && to->elementType && isImplicitlyConvertible(from->elementType, to->elementType);

    //  null — пустая ссылка на объект класса. Struct всегда value-тип.
    if (from->kind == TypeKind::Null) {
        return to->kind == TypeKind::Class;
    }

    return false;
}

static bool isUnsupportedEqualityType(const std::shared_ptr<Type>& type) {
    if (!type) return true;

    return type->kind == TypeKind::Struct
        || type->kind == TypeKind::Array
        || type->kind == TypeKind::DynArray;
}

static bool integerLiteralFitsType(double value, const std::shared_ptr<Type>& type) {
    if (!type) return true;

    switch (type->kind) {
        case TypeKind::Int8:
            return value >= static_cast<double>(std::numeric_limits<int8_t>::lowest())
                && value <= static_cast<double>(std::numeric_limits<int8_t>::max());
        case TypeKind::Int16:
            return value >= static_cast<double>(std::numeric_limits<int16_t>::lowest())
                && value <= static_cast<double>(std::numeric_limits<int16_t>::max());
        case TypeKind::Int32:
            return value >= static_cast<double>(std::numeric_limits<int32_t>::lowest())
                && value <= static_cast<double>(std::numeric_limits<int32_t>::max());
        case TypeKind::Int64:
            return value >= static_cast<double>(std::numeric_limits<int64_t>::lowest())
                && value <= static_cast<double>(std::numeric_limits<int64_t>::max());

        case TypeKind::Uint8:
            return value >= 0.0
                && value <= static_cast<double>(std::numeric_limits<uint8_t>::max());
        case TypeKind::Uint16:
            return value >= 0.0
                && value <= static_cast<double>(std::numeric_limits<uint16_t>::max());
        case TypeKind::Uint32:
            return value >= 0.0
                && value <= static_cast<double>(std::numeric_limits<uint32_t>::max());
        case TypeKind::Uint64:
            return value >= 0.0
                && value <= static_cast<double>(std::numeric_limits<uint64_t>::max());

        case TypeKind::Char:
            return value >= 0.0
                && value <= static_cast<double>(std::numeric_limits<unsigned char>::max());

        default:
            return true;
    }
}

Expr* SemanticAnalyzer::makeDefaultExprForType(const std::shared_ptr<Type>& type, int line, int column) {
    if (!type) return nullptr;

    Expr* expr = nullptr;

    switch (type->kind) {
        case TypeKind::Bool: {
            auto* node = new Bool();
            node->value = false;
            expr = node;
            break;
        }

        case TypeKind::Char: {
            auto* node = new CharLiteral();
            node->value = '0';
            expr = node;
            break;
        }

        case TypeKind::String: {
            auto* node = new String();
            node->value = "NULL";
            expr = node;
            break;
        }

        case TypeKind::Int8:
        case TypeKind::Int16:
        case TypeKind::Int32:
        case TypeKind::Int64:
        case TypeKind::Uint8:
        case TypeKind::Uint16:
        case TypeKind::Uint32:
        case TypeKind::Uint64:
        case TypeKind::Float32:
        case TypeKind::Float64: {
            auto* node = new Number();
            node->value = 0;
            node->isFloat = isFloatType(type);
            expr = node;
            break;
        }

        case TypeKind::DynArray: {
            auto* node = new ArrayLiteral();
            node->resolvedType = type;
            expr = node;
            break;
        }

        case TypeKind::Array: {
            auto* node = new ArrayLiteral();

            for (int i = 0; i < type->arraySize; ++i) {
                node->elements.push_back(makeDefaultExprForType(type->elementType, line, column));
            }

            node->resolvedType = type;
            expr = node;
            break;
        }

        case TypeKind::Struct: {
            auto* node = new StructLiteral();
            node->name = type->name;
            node->resolvedType = type;
            expr = node;
            break;
        }

        case TypeKind::Class: {
            expr = new NullLiteral();
            break;
        }

        case TypeKind::Void:
        default:
            return nullptr;
    }

    expr->line = line;
    expr->column = column;
    expr->resolvedType = type;
    return expr;
}

static ParamInfo makeParamInfo(const Param& param, const std::shared_ptr<Type>& paramType) {
    ParamInfo info;
    info.name = param.name;
    info.type = paramType;
    info.defaultValue = param.defaultValue;
    info.isConst = param.isConst;
    return info;
}

FieldInfo* findFieldInTypeSymbol(std::shared_ptr<Symbol> sym, const std::string& fieldName) {
    if (!sym) {
        return nullptr;
    }

    std::vector<FieldInfo>* fields = nullptr;

    if (sym->kind == SymbolKind::Struct && sym->structInfo) {
        fields = &sym->structInfo->fields;
    }
    else if (sym->kind == SymbolKind::Class && sym->classInfo) {
        fields = &sym->classInfo->fields;
    }

    if (!fields) {
        return nullptr;
    }

    for (auto& field : *fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }

    return nullptr;
}
//  Проверяет допустимость явного приведения cast<To>(from)
//  int -> int, int -> float, float -> int, float -> float,
//  int -> bool, char -> int.  string -> числовые — нельзя.
static bool isCastable(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;

    if (typesEqual(from, to)) return true;

    bool fromInt = isIntType(from);
    bool toInt = isIntType(to);
    bool fromFloat = isFloatType(from);
    bool toFloat = isFloatType(to);

    if ((fromInt || fromFloat) && (toInt || toFloat)) {
        return true;
    }
    
    if (fromInt && to->kind == TypeKind::Bool) {
        return true;
    }

    if (from->kind == TypeKind::Bool && toInt) {
        return true;
    }

    if (from->kind == TypeKind::Char && toInt) {
        return true;
    }

    if (fromInt && to->kind == TypeKind::Char) {
        return true;
    }

    if (from->kind == TypeKind::String && to->kind == TypeKind::Bool) {
        return true;
    }

    if (from->kind == TypeKind::Bool && to->kind == TypeKind::String) {
        return true;
    }

    return false;
}

//  Определяет общий тип для бинарной операции (к чему привести оба операнда)
//  Например: int32 + int64 -> int64, int + float -> float64
//  Возвращает nullptr если типы несовместимы
static std::shared_ptr<Type> commonType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return nullptr;

    if (typesEqual(a, b)) return a;  //  одинаковые — ничего приводить не надо

    //  int/uint + float → больший float-тип. Так int + float32 остаётся float32,
    //  а всё с float64 расширяется до float64.
    if ((isIntType(a) && isFloatType(b)) || (isFloatType(a) && isIntType(b))) {
        if (a->kind == TypeKind::Float64 || b->kind == TypeKind::Float64) {
            return makeType(TypeKind::Float64);
        }
        return makeType(TypeKind::Float32);
    }

    //  Оба float — берём больший
    if (isFloatType(a) && isFloatType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

    //  Оба signed int — берём больший
    if (isSignedIntType(a) && isSignedIntType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

    //  Оба unsigned int — берём больший
    if (isUnsignedIntType(a) && isUnsignedIntType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

    //  Signed + unsigned: выбираем signed-тип, который может представить оба
    //  диапазона. Для int64 + uint64 целого общего типа нет, поднимаем до float64.
    if ((isSignedIntType(a) && isUnsignedIntType(b)) || (isUnsignedIntType(a) && isSignedIntType(b))) {
        auto signedType = isSignedIntType(a) ? a : b;
        auto unsignedType = isUnsignedIntType(a) ? a : b;
        int signedBits = intBitWidth(signedType);
        int unsignedBits = intBitWidth(unsignedType);
        int commonBits = signedBits;

        if (signedBits <= unsignedBits) {
            commonBits = unsignedBits * 2;
        }

        if (commonBits <= 64) {
            return makeType(signedIntKindForBits(commonBits));
        }
        return makeType(TypeKind::Float64);
    }

    return nullptr;  //  несовместимы 
}

std::shared_ptr<Type> SemanticAnalyzer::ensureVariableTypeKnown(const std::shared_ptr<Symbol>& sym, int line, int column) {
    if (!sym) {
        return nullptr;
    }

    if (sym->type) {
        return sym->type;
    }

    if (sym->kind != SymbolKind::Variable || !sym->isAuto) {
        return sym->type;
    }

    if (!sym->autoInit) {
        error(line, column, "cannot infer type of auto variable '" + sym->name + "'");
        return nullptr;
    }

    if (sym->isResolvingAuto) {
        error(line, column, "cyclic auto type inference for variable '" + sym->name + "'");
        return nullptr;
    }

    sym->isResolvingAuto = true;

    auto inferred = analyzeExpr(sym->autoInit);

    sym->isResolvingAuto = false;

    if (!inferred) {
        error(line, column, "cannot infer type of auto variable '" + sym->name + "'");
        return nullptr;
    }

    sym->type = inferred;

    if (sym->isConst && isIntType(sym->type)) {
        auto value = evalConstIntExpr(sym->autoInit);
        if (value.has_value()) {
            sym->intConstValue = *value;
        }
    }

    return sym->type;
}

std::shared_ptr<Type> SemanticAnalyzer::ensureAliasTypeKnown(const std::shared_ptr<Symbol>& sym, int line, int column) {
    if (!sym) {
        return nullptr;
    }

    if (sym->kind != SymbolKind::TypeAlias) {
        return sym->type;
    }

    if (sym->type) {
        return sym->type;
    }

    if (!sym->aliasTarget) {
        error(line, column, "type alias '" + sym->name + "' has no target type");
        return nullptr;
    }

    if (sym->isResolvingAlias) {
        error(line, column, "cyclic type alias '" + sym->name + "'");
        return nullptr;
    }

    sym->isResolvingAlias = true;

    auto resolved = resolveTypeName(sym->aliasTarget);

    sym->isResolvingAlias = false;

    if (!resolved) {
        error(line, column, "unknown type '" + typeNameToString(sym->aliasTarget) + "' in type alias '" + sym->name + "'");
        return nullptr;
    }

    sym->type = resolved;
    return sym->type;
}

//  Регистрация встроенных функций
//  Вызывается в начале analyze(), до анализа остального кода
//  Каждая builtin-функция добавляется в глобальный scope как Symbol с kind=Function

void SemanticAnalyzer::registerBuiltins() {
    //  print — принимает один аргумент любого типа, возвращает void
    {
        auto sym = std::make_shared<Symbol>(); //  make_shared позволяет нескольким владельцам ссылаться на один символ из таблицы
        sym->name = "print";
        sym->kind = SymbolKind::Function;
        sym->type = makeType(TypeKind::Void);   //  print ничего не возвращает
        sym->funcInfo = std::make_shared<FuncInfo>();   //  Указатель на поля функции  
        sym->funcInfo->returnType = makeType(TypeKind::Void);   //  Какой тип на самом деле функция возвращает
        //  параметры не фиксируем — print принимает что угодно
        table.declare(sym); //  Все встроенные функции по умолчанию лежат в таблице
    }
    //  input — без аргументов, возвращаемый тип берётся из контекста
    {
        auto sym = std::make_shared<Symbol>();
        sym->name = "input";
        sym->kind = SymbolKind::Function;
        sym->type = nullptr;    //  auto тип 
        sym->funcInfo = std::make_shared<FuncInfo>();
        sym->funcInfo->returnType = nullptr;
        table.declare(sym);
    }
    //  len — принимает массив или строку, возвращает int32
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



std::optional<long long> SemanticAnalyzer::evalConstIntExpr(Expr* expr) {
    if (!expr) {
        return std::nullopt;
    }

    if (auto* num = dynamic_cast<Number*>(expr)) {
        if (num->isFloat) {
            return std::nullopt;
        }

        return static_cast<long long>(num->value);
    }

    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        auto sym = table.resolve(id->name);

        if (!sym || !sym->isConst || !sym->intConstValue.has_value()) {
            return std::nullopt;
        }

        return sym->intConstValue;
    }

    if (auto* ns = dynamic_cast<NamespaceAccess*>(expr)) {
        auto sym = resolveNamespaceAccess(ns);

        if (!sym || !sym->isConst || !sym->intConstValue.has_value()) {
            return std::nullopt;
        }

        return sym->intConstValue;
    }

    if (auto* unary = dynamic_cast<Unary*>(expr)) {
        auto value = evalConstIntExpr(unary->operand);

        if (!value.has_value()) {
            return std::nullopt;
        }

        switch (unary->op) {
            case Operand::UnaryPlus:
                return *value;

            case Operand::UnaryMinus:
                return -*value;

            default:
                return std::nullopt;
        }
    }

    if (auto* bin = dynamic_cast<Binary*>(expr)) {
        auto left = evalConstIntExpr(bin->left);
        auto right = evalConstIntExpr(bin->right);

        if (!left.has_value() || !right.has_value()) {
            return std::nullopt;
        }

        switch (bin->op) {
            case Operand::Add:
                return *left + *right;

            case Operand::Sub:
                return *left - *right;

            case Operand::Mul:
                return *left * *right;

            case Operand::Div:
                if (*right == 0) {
                    return std::nullopt;
                }
                return *left / *right;

            case Operand::Mod:
                if (*right == 0) {
                    return std::nullopt;
                }
                return *left % *right;

            default:
                return std::nullopt;
        }
    }

    return std::nullopt;
}

std::shared_ptr<Type> SemanticAnalyzer::resolveArrayTypeSuffix(std::shared_ptr<Type> elemType, const TypeSuffix& suffix, int line, int column) {
    if (suffix.isDynamic) {
        return makeDynArrayType(elemType);
    }

    // T[3] — статический массив
    if (!suffix.size) {
        error(line, column, "fixed array requires a size expression");
        return nullptr;
    }

    auto sizeType = analyzeExpr(suffix.size, nullptr);

    if (!sizeType) {
        return nullptr;
    }

    if (!isIntType(sizeType)) {
        error(line, column, "array size expression must have integer type");
        return nullptr;
    }

    auto constSize = evalConstIntExpr(suffix.size);

    if (!constSize.has_value()) {
        error(line, column, "fixed array size must be a compile-time integer constant");
        return nullptr;
    }

    if (*constSize < 0) {
        error(line, column, "array size must be non-negative");
        return nullptr;
    }

    if (*constSize > std::numeric_limits<int>::max()) {
        error(line, column, "array size is too large");
        return nullptr;
    }

    return makeArrayType(elemType, static_cast<int>(*constSize));
}

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeName(TypeName* typeName) {
    if (!typeName) {
        return nullptr;
    }

    std::shared_ptr<Type> baseType;

    const std::string& name = typeName->base;

    if (name == "int" || name == "int32")       baseType = makeType(TypeKind::Int32);
    else if (name == "int8")                    baseType = makeType(TypeKind::Int8);
    else if (name == "int16")                   baseType = makeType(TypeKind::Int16);
    else if (name == "int64")                   baseType = makeType(TypeKind::Int64);
    else if (name == "uint" || name == "uint32") baseType = makeType(TypeKind::Uint32);
    else if (name == "uint8")                   baseType = makeType(TypeKind::Uint8);
    else if (name == "uint16")                  baseType = makeType(TypeKind::Uint16);
    else if (name == "uint64")                  baseType = makeType(TypeKind::Uint64);
    else if (name == "float" || name == "float64") baseType = makeType(TypeKind::Float64);
    else if (name == "float32")                 baseType = makeType(TypeKind::Float32);
    else if (name == "bool")                    baseType = makeType(TypeKind::Bool);
    else if (name == "char")                    baseType = makeType(TypeKind::Char);
    else if (name == "string")                  baseType = makeType(TypeKind::String);
    else if (name == "void")                    baseType = makeType(TypeKind::Void);
    else {
        auto sym = resolveQualifiedSymbol(name);
        if (!sym) {
            return nullptr;
        }

        if (sym->kind == SymbolKind::Struct) {
            baseType = makeType(TypeKind::Struct);
            baseType->name = sym->type ? sym->type->name : name;
        }
        else if (sym->kind == SymbolKind::Class) {
            baseType = makeType(TypeKind::Class);
            baseType->name = sym->type ? sym->type->name : name;
        }
        else if (sym->kind == SymbolKind::TypeAlias) {
            baseType = ensureAliasTypeKnown(sym, 0, 0);

            if (!baseType) {
                return nullptr;
            }
        }
        else {
            return nullptr;
        }
    }

    std::shared_ptr<Type> result = baseType;

    for (const auto& suffix : typeName->suffixes) {
        result = resolveArrayTypeSuffix(result, suffix, suffix.size ? suffix.size->line : 0, suffix.size ? suffix.size->column : 0);
        if (!result) {
            return nullptr;
        }
    }

    return result;
}

//  Резолвит тип объявления (поля структуры/класса или параметра функции).
//  Поддерживает:
//    - явный тип: резолвит typeName, при наличии defaultValue проверяет совместимость;
//    - auto:      требует defaultValue, выводит тип из него;
//    - отсутствие auto без typeName — ошибка (защита от мусора в AST).
//  Параметр what — описание для текста ошибок (напр. "field 'x' of struct 'Point'").
std::shared_ptr<Type> SemanticAnalyzer::resolveDeclaredType(bool isAuto, bool isConst, TypeName* typeName, Expr*& defaultValue, int line, int column, const std::string& what, DeclContext context) {
    if (isAuto) {
        if (!defaultValue) {
            error(line, column, "'auto' " + what + " requires a default value to infer type");
            return nullptr;
        }
        auto inferred = analyzeExpr(defaultValue);
        if (!inferred) {
            error(line, column, "cannot infer type for 'auto' " + what);
        }
        return inferred;
    }

    auto declared = resolveTypeName(typeName);
    if (!declared) {
        error(line, column, "unknown type '" + typeNameToString(typeName) + "' for " + what);
        return nullptr;
    }
    else if (declared->kind == TypeKind::Void) {
        error(line, column, "type 'void' is not allowed for " + what);
        return declared;
    }

    if (defaultValue) {
        auto defType = analyzeExpr(defaultValue, declared);

        if (defType && !isImplicitlyConvertible(defType, declared)) {
            error(line, column, "default value of " + what + ": cannot convert '" + typeToString(defType) + "' to '" + typeToString(declared) + "'");
        }

        return declared;
    }

    if (context == DeclContext::Variable && isConst) {
        error(line, column, "const " + what + " requires an explicit initializer");
        return declared;
    }

    if (context == DeclContext::Field) {
        defaultValue = makeDefaultExprForType(declared, line, column);
    }

    return declared;
}

static std::vector<std::string> splitQualifiedName(const std::string& name) {
    std::vector<std::string> parts;

    size_t start = 0;

    while (start < name.size()) {
        size_t pos = name.find("::", start);

        if (pos == std::string::npos) {
            parts.push_back(name.substr(start));
            break;
        }

        parts.push_back(name.substr(start, pos - start));
        start = pos + 2;
    }

    return parts;
}

static std::string appendQualifiedName(const std::string& prefix, const std::string& name) {
    if (prefix.empty()) {
        return name;
    }

    return prefix + "::" + name;    //  Добавляем имя пространства имён к имени сущности если она ей принадлежит
}

std::shared_ptr<Symbol> SemanticAnalyzer::resolveQualifiedSymbol(const std::string& nameSpace, const std::string& member) {
    auto parts = splitQualifiedName(nameSpace);
    parts.push_back(member);

    if (parts.empty()) {
        return nullptr;
    }

    std::shared_ptr<Symbol> currentSym = table.resolve(parts[0]);
    std::shared_ptr<Scope> currentScope = nullptr;

    if (!currentSym) {
        return nullptr;
    }

    for (size_t i = 1; i < parts.size(); i++) {
        // namespace::...
        if (currentSym->kind == SymbolKind::Namespace) {
            if (!currentSym->namespaceScope) {
                return nullptr;
            }

            currentScope = currentSym->namespaceScope;

            auto it = currentScope->symbols.find(parts[i]);
            if (it == currentScope->symbols.end()) {
                return nullptr;
            }

            currentSym = it->second;
            continue;
        }

        // Class::NestedStruct
        if (currentSym->kind == SymbolKind::Class) {
            if (!currentSym->classInfo) {
                return nullptr;
            }

            auto it = currentSym->classInfo->nestedStructs.find(parts[i]);
            if (it == currentSym->classInfo->nestedStructs.end()) {
                return nullptr;
            }

            auto nestedSym = std::make_shared<Symbol>();
            nestedSym->name = it->second->name;
            nestedSym->kind = SymbolKind::Struct;
            nestedSym->structInfo = it->second;

            auto type = makeType(TypeKind::Struct);
            type->name = it->second->name;
            nestedSym->type = type;

            currentSym = nestedSym;
            continue;
        }

        return nullptr;
    }

    return currentSym;
}

//  Маршалинг для extern-C вызовов согласно спецификации:
//  string -> const char*    (String и так совпадает с String, мапленным из char*)
//  array  -> pointer to first element  (Array/DynArray принимаются там, где C ждёт uint64)
//  ptr    -> uint64                    (uint64 -> uint64, тривиально)
//  Любой другой нечисловой/непримитивный тип на стороне MyLang в C-параметр
//  не маршализуется и должен быть отвергнут с ошибкой.
static bool isMarshalableToC(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;
    //  Стандартный widening покрывает совпадения и числовое сужение/расширение.
    if (isImplicitlyConvertible(from, to)) return true;
    //  C-указатели мапятся в Uint64: туда же отправляем строки и массивы.
    if (to->kind == TypeKind::Uint64) {
        return from->kind == TypeKind::String
            || from->kind == TypeKind::Array
            || from->kind == TypeKind::DynArray;
    }
    return false;
}

//  Унифицирует проверку вызова функции/метода по объявленной сигнатуре.
//  Согласно спецификации: количество аргументов должно совпадать (для variadic — не меньше
//  числа фиксированных), а каждый аргумент должен быть widening-конвертируем в тип параметра
//  через isImplicitlyConvertible.  Для extern-C-функций дополнительно применяются правила
//  маршалинга (string/array/ptr -> uint64).  Параметр what — описание для ошибок.
void SemanticAnalyzer::checkCallArguments(const std::string& what, const std::vector<ParamInfo>& params,
    const std::vector<std::shared_ptr<Type>>& argTypes, bool variadic, int line, int column, bool isExternC) {
    size_t fixed = params.size();
    bool arityOk;
    if (variadic) {
        arityOk = argTypes.size() >= fixed;
    }
    else {
        arityOk = argTypes.size() <= fixed;
    }

    if (!arityOk) {
        if (variadic) {
            error(line, column, what + " expects at least " + std::to_string(fixed) + " arguments, got " + std::to_string(argTypes.size()));
        } else {
            error(line, column, what + " expects at most " + std::to_string(fixed) + " arguments, got " + std::to_string(argTypes.size()));
        }
        return;
    }

    size_t provided = std::min(argTypes.size(), fixed);
    for (size_t j = 0; j < provided; j++) {
        auto expected = params[j].type;
        if (!argTypes[j] || !expected) continue;
        bool ok = isExternC ? isMarshalableToC(argTypes[j], expected) : isImplicitlyConvertible(argTypes[j], expected);
        
        if (!ok) {
            const char* verb = isExternC ? "marshal" : "convert";
            error(line, column, what + " argument " + std::to_string(j + 1) + ": cannot " + verb + " '" + typeToString(argTypes[j]) + "' to '" + typeToString(expected) + "'");
        }
    }

    //  Variadic-хвост extern-C функции (printf и т.п.) — каждое лишнее значение тоже
    //  должно быть представимо в ABI: либо примитив, либо маршализуемое в uint64.
    if (isExternC && variadic) {
        for (size_t j = fixed; j < argTypes.size(); j++) {
            auto t = argTypes[j];
            if (!t) continue;
            bool ok = isNumericType(t)
                || t->kind == TypeKind::Bool
                || t->kind == TypeKind::Char
                || t->kind == TypeKind::String
                || t->kind == TypeKind::Array
                || t->kind == TypeKind::DynArray
                || t->kind == TypeKind::Uint64;
            if (!ok) {
                error(line, column, what + " variadic argument " + std::to_string(j + 1) + ": type '" + typeToString(t) + "' is not marshalable to C");
            }
        }
    }
}

void SemanticAnalyzer::appendMissingDefaultArgs(FuncCall* call, const std::vector<ParamInfo>& params, bool variadic) {
    if (!call || variadic) {
        return;
    }

    if (call->args.size() >= params.size()) {
        return;
    }

    for (size_t i = call->args.size(); i < params.size(); i++) {
        Expr* defaultArg = nullptr;

        if (params[i].defaultValue) {
            defaultArg = params[i].defaultValue;
        }
        else {
            defaultArg = makeDefaultExprForType(params[i].type, call->line, call->column);
        }

        if (!defaultArg) {
            error(call->line, call->column, "cannot create default argument for parameter '" + params[i].name + "'");
            continue;
        }

        call->args.push_back(defaultArg);
    }
}

//  Перегрузка для случая, когда в строке уже лежит полный квалифицированный путь
//  ("Foo::Bar::Baz"). Разделяем на namespace-путь и последний идентификатор.
//  Если "::" нет — ищем как обычный символ в текущей таблице.
std::shared_ptr<Symbol> SemanticAnalyzer::resolveQualifiedSymbol(const std::string& qualifiedName) {
    auto sep = qualifiedName.rfind("::");
    if (sep == std::string::npos) {
        auto sym = table.resolve(qualifiedName);

        if (!sym && currentClass) {
            auto nested = currentClass->nestedStructs.find(qualifiedName);
            if (nested != currentClass->nestedStructs.end()) {
                auto nestedSym = std::make_shared<Symbol>();
                nestedSym->name = qualifiedName;
                nestedSym->kind = SymbolKind::Struct;
                nestedSym->structInfo = nested->second;

                auto type = makeType(TypeKind::Struct);
                type->name = nested->second->name;
                nestedSym->type = type;

                return nestedSym;
            }
        }

        return sym;
    }
    return resolveQualifiedSymbol(qualifiedName.substr(0, sep), qualifiedName.substr(sep + 2));
}

std::shared_ptr<Symbol> SemanticAnalyzer::resolveNamespaceAccess(NamespaceAccess* access) {
    if (!access) {
        return nullptr;
    }

    return resolveQualifiedSymbol(access->nameSpace, access->member);
}


// Обход AST

//  Спускается по lvalue-выражению (Identifier / FieldAccess / ArrayAccess / NamespaceAccess)
//  до корневого символа — нужно для проверки const на цель присваивания и ++/--.
std::shared_ptr<Symbol> SemanticAnalyzer::resolveTargetRoot(Expr* e) {
    if (!e) return nullptr;
    if (auto* id = dynamic_cast<Identifier*>(e)) {
        return table.resolve(id->name);
    }
    if (auto* fa = dynamic_cast<FieldAccess*>(e)) {
        return resolveTargetRoot(fa->object);
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(e)) {
        return resolveTargetRoot(aa->object);
    }
    if (auto* na = dynamic_cast<NamespaceAccess*>(e)) {
        return resolveNamespaceAccess(na);
    }
    return nullptr;
}

//  Является ли выражение корректным lvalue: тем, у чего есть устойчивое
//  место в памяти, в которое можно записать или над которым выполнить ++/--.
//  Lvalue: переменная (Identifier→Variable), поле lvalue-объекта, элемент
//  lvalue-массива, namespace-доступ к переменной. Всё остальное (литералы,
//  бинарные/унарные выражения, вызовы функций, cast) — rvalue.
bool SemanticAnalyzer::isLvalue(Expr* e) {
    if (!e) return false;
    if (auto* id = dynamic_cast<Identifier*>(e)) {
        auto sym = table.resolve(id->name);
        if (!sym && currentClass) {
            //  Неявный self.<field> — это lvalue (запись в поле текущего экземпляра).
            for (auto& f : currentClass->fields) {
                if (f.name == id->name) return true;
            }
        }
        return sym && sym->kind == SymbolKind::Variable;
    }
    if (auto* fa = dynamic_cast<FieldAccess*>(e)) {
        return isLvalue(fa->object);
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(e)) {
        return isLvalue(aa->object);
    }
    if (auto* na = dynamic_cast<NamespaceAccess*>(e)) {
        auto sym = resolveNamespaceAccess(na);
        return sym && sym->kind == SymbolKind::Variable;
    }
    return false;
}

std::string SemanticAnalyzer::nonValueSymbolMessage(const std::shared_ptr<Symbol>& sym, const std::string& name) const {
    if (!sym) {
        return "'" + name + "' is not declared";
    }

    switch (sym->kind) {
        case SymbolKind::Function:
            return "'" + name + "' is a function, not a value; call it with '()'";

        case SymbolKind::Struct:
            return "'" + name + "' is a struct type, not a value";

        case SymbolKind::Class:
            return "'" + name + "' is a class type, not a value; construct it with '()'";

        case SymbolKind::TypeAlias:
            return "'" + name + "' is a type alias, not a value";

        case SymbolKind::Namespace:
            return "'" + name + "' is a namespace, not a value";

        case SymbolKind::Variable:
            return "";

        default:
            return "'" + name + "' is not a value";
    }
}

std::shared_ptr<Type> SemanticAnalyzer::analyzeExpr(Expr* expr, std::shared_ptr<Type> expected) {
    if (!expr) return nullptr;  //  В случае int x; без инициализатора — ничего не делаем

    if (auto* num = dynamic_cast<Number*>(expr)) {
        if (num->isFloat) {
            if (expected && isFloatType(expected)) {
                expr->resolvedType = expected;
            }
            else {
                expr->resolvedType = makeType(TypeKind::Float64);
            }
        } 
        else {
            if (expected && isIntType(expected)) {
                if (!integerLiteralFitsType(num->value, expected)) {
                    error(expr->line, expr->column, "integer literal '" + std::to_string(static_cast<long long>(num->value)) + "' does not fit into type '" + typeToString(expected) + "'");
                }

                expr->resolvedType = expected;
            }
            else if (expected && isFloatType(expected)) {
                expr->resolvedType = expected;
            }
            else {
                expr->resolvedType = makeType(TypeKind::Int32);
            }
        }

        return expr->resolvedType;
    }

    //  Строковый литерал -> string, либо char (если ожидается Char и длина = 1)
    if (auto* s = dynamic_cast<String*>(expr)) {
        if (expected && expected->kind == TypeKind::Char && s->value.size() == 1) {
            expr->resolvedType = makeType(TypeKind::Char);
        }
        else {
            expr->resolvedType = makeType(TypeKind::String);
        }
        return expr->resolvedType;
    }

    //  Булев литерал -> bool
    if (dynamic_cast<Bool*>(expr)) {
        expr->resolvedType = makeType(TypeKind::Bool);
        return expr->resolvedType;
    }

    //  Символьный литерал -> char
    if (dynamic_cast<CharLiteral*>(expr)) {
        expr->resolvedType = makeType(TypeKind::Char);
        return expr->resolvedType;
    }

    //  Литерал null — пустая ссылка на объект класса.
    //  При наличии class-контекста принимает его, иначе остаётся типом Null.
    if (dynamic_cast<NullLiteral*>(expr)) {
        if (expected && expected->kind == TypeKind::Class) {
            expr->resolvedType = expected;
        }
        else {
            expr->resolvedType = makeType(TypeKind::Null);
        }
        return expr->resolvedType;
    }

    //  Идентификатор
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        auto sym = table.resolve(id->name);
        if (!sym) {
            //  Внутри метода/конструктора/деструктора имя поля класса разрешается
            //  как неявное self.<имя> — сами поля в scope не объявляются,
            //  поэтому сначала проверяем текущий класс.
            if (currentClass) {
                for (auto& f : currentClass->fields) {
                    if (f.name == id->name) {
                        id->resolvedField = &f;
                        expr->resolvedType = f.type;
                        return expr->resolvedType;
                    }
                }
            }
            error(expr->line, expr->column, "'" + id->name + "' is not declared");
            return nullptr;
        }

        if (sym->kind != SymbolKind::Variable) {
            error(expr->line, expr->column, nonValueSymbolMessage(sym, id->name));
            return nullptr;
        }
        auto knownType = ensureVariableTypeKnown(sym, expr->line, expr->column);
        if (!knownType) {
            return nullptr;
        }

        if (!sym->type) {
            error(expr->line, expr->column, "cannot use variable '" + id->name + "' before its type is known");
            return nullptr;
        }

        id->resolvedSym = sym;
        //  По спецификации использование неинициализированной переменной не ошибка —
        //  она автоматически инициализируется значением по умолчанию своего типа.
        expr->resolvedType = knownType;
        return expr->resolvedType;
    }

    //  Бинарная операция
    if (auto* bin = dynamic_cast<Binary*>(expr)) {  //  Рекурсивно анализируем оба операнда, потом проверяем совместимость типов
        auto leftType = analyzeExpr(bin->left, expected);
        auto rightType = analyzeExpr(bin->right, expected);

        if (!leftType || !rightType) return nullptr;    //  Если хотя бы один операнд не определён — не можем проверить

        switch (bin->op) {  //  Операнд
            case Operand::Add:
                if (leftType->kind == TypeKind::String && rightType->kind == TypeKind::String) {
                    expr->resolvedType = makeType(TypeKind::String);  //  "Hello" + "World" -> string
                    return expr->resolvedType;
                }
                //  string + char  /  char + string -> string (символ склеивается как 1-байтовая строка)
                if ((leftType->kind == TypeKind::String && rightType->kind == TypeKind::Char)
                 || (leftType->kind == TypeKind::Char && rightType->kind == TypeKind::String)) {
                    expr->resolvedType = makeType(TypeKind::String);
                    return expr->resolvedType;
                }
                [[fallthrough]];  //  иначе — обычная арифметика

            case Operand::Sub:
            case Operand::Mul:
            case Operand::Div:
            case Operand::Mod:
            case Operand::Pow:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, expr->column, "arithmetic operator requires numeric operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (bin->op == Operand::Mod) {
                    if (!isIntType(leftType) || !isIntType(rightType)) {
                        error(bin->line, bin->column, "operator '%' requires integer operands");
                        return nullptr;
                    }
                }
                {
                    auto common = commonType(leftType, rightType);  //  Неявное приведение: int32 + int64 -> int64, int + float -> float64
                    if (!common) {
                        error(expr->line, expr->column, "incompatible types in arithmetic: '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                        return nullptr;
                    }
                    if (bin->op == Operand::Pow && isIntType(common) && isNegativeIntegerLiteralExpr(bin->right)) {
                        common = makeType(TypeKind::Float64);
                    }
                    expr->resolvedType = common;  
                }
                return expr->resolvedType;

            case Operand::Less:     //  Сравнения
            case Operand::Greater:
            case Operand::LessEqual:
            case Operand::GreaterEqual:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, expr->column, "comparison requires numeric operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (!commonType(leftType, rightType)) {
                    error(expr->line, expr->column, "incompatible types in comparison: '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);  //  Возвращаемый тип вседа bool
                return expr->resolvedType;

            case Operand::EqualEqual:   //  Равенства
            case Operand::NotEqual:
                if (isUnsupportedEqualityType(leftType) || isUnsupportedEqualityType(rightType)) {
                    error(expr->line, expr->column, "operator '" + std::string(bin->op == Operand::EqualEqual ? "==" : "!=") +
                        "' is not defined for '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (isNumericType(leftType) && isNumericType(rightType)) {
                    if (!commonType(leftType, rightType)) {
                        error(expr->line, expr->column, "incompatible types in comparison: '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                        return nullptr;
                    }
                    expr->resolvedType = makeType(TypeKind::Bool);
                    return expr->resolvedType;
                }
                if (!isImplicitlyConvertible(leftType, rightType) && !isImplicitlyConvertible(rightType, leftType)) {
                    error(expr->line, expr->column, "cannot compare '" + typeToString(leftType) + "' with '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            case Operand::And:      //  И, ИЛИ
            case Operand::Or:
                if (leftType->kind != TypeKind::Bool || rightType->kind != TypeKind::Bool) {
                    error(expr->line, expr->column, "logical operator requires bool operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            default:
                return nullptr;
        }
    }

    //  Унарная операция 
    if (auto* unary = dynamic_cast<Unary*>(expr)) {
        std::shared_ptr<Type> operandExpected = nullptr;
        if ((unary->op == Operand::UnaryMinus || unary->op == Operand::UnaryPlus) && expected && isNumericType(expected)) {
            operandExpected = expected;
        }

        auto operandType = analyzeExpr(unary->operand, operandExpected); //  Чё мы пытаемся унарнуть
        if (!operandType) return nullptr;

        switch (unary->op) {
            case Operand::UnaryMinus:
                if (!isNumericType(operandType)) {
                    error(expr->line, expr->column, "unary - requires numeric operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }

                // --- СПЕЦИАЛЬНЫЙ СЛУЧАЙ: -<числовой литерал> ---
                if (expected) {
                    if (auto* num = dynamic_cast<Number*>(unary->operand)) {
                        if (!num->isFloat) { // только для целых литералов
                            double negValue = -num->value;

                            if (!integerLiteralFitsType(negValue, expected)) {
                                error(expr->line, expr->column, "integer literal " + std::to_string((long long)negValue) + " does not fit in type '" + typeToString(expected) + "'");
                                return nullptr;
                            }

                            expr->resolvedType = expected;
                            return expr->resolvedType;
                        }
                    }
                }

                // fallback — обычное поведение
                expr->resolvedType = operandType;
                return expr->resolvedType;

            case Operand::UnaryPlus:
                if (!isNumericType(operandType)) {  //  Обязано быть число для + и -
                    error(expr->line, expr->column, "unary + requires numeric operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            case Operand::Not:
                if (operandType->kind != TypeKind::Bool) {  //  Для отрицания всегда bool
                    error(expr->line, expr->column, "'!' requires bool operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            case Operand::Increment:
            case Operand::Decrement:
                if (!isIntType(operandType)) {  //  Только целые числа
                    error(expr->line, expr->column, "++/-- requires integer operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }
                if (!isLvalue(unary->operand)) {    //  foo()++, 4--, (a + b)-- — операнд должен быть lvalue
                    error(expr->line, expr->column, "++/-- requires an lvalue operand");
                    return nullptr;
                }
                {
                    auto rootSym = resolveTargetRoot(unary->operand);
                    if (rootSym && rootSym->isConst) {
                        error(expr->line, expr->column, "cannot apply ++/-- to const '" + rootSym->name + "'");
                    }
                    if (auto* id = dynamic_cast<Identifier*>(unary->operand)) {
                        if (id->resolvedField && id->resolvedField->isConst) {
                            error(expr->line, expr->column, "cannot apply ++/-- to const field '" + id->name + "'");
                        }
                    }
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            default:
                return nullptr;
        }
    }

    //  Вызов функции 
    if (auto* call = dynamic_cast<FuncCall*>(expr)) {   
        
        //  Вызов метода класса
        if (auto* fieldCallee = dynamic_cast<FieldAccess*>(call->callee)) { 
            auto objType = analyzeExpr(fieldCallee->object);    //  Анализ объекта чей метод мы вызываем 
            
            if (!objType || objType->kind != TypeKind::Class) { //  Если пытаемся вызвать метод не у класса 
                for (auto* arg : call->args) {
                    analyzeExpr(arg);   //  То сперва анализируем все параметры чтобы собрать все потенциальные ошибки
                }
                if (objType)    //  И только потом уже выходим со всем списком найденных проблем
                    error(expr->line, expr->column, "method call on non-class type '" + typeToString(objType) + "'");
                return nullptr;
            }

            auto classSym = resolveQualifiedSymbol(objType->name);   //  Ищем класс в таблице символов
            if (!classSym || !classSym->classInfo) {
                return nullptr;
            }

            const std::string& methodName = fieldCallee->field; 
            auto method = classSym->classInfo->methods.find(methodName);    //  Ищем метод в классе
            if (method == classSym->classInfo->methods.end()) {
                error(expr->line, expr->column, "class '" + objType->name + "' has no method '" + methodName + "'");
                for (auto* arg : call->args) {
                    analyzeExpr(arg);   //  Также снова проходимся по параметрам для сбора ошибок
                }
                return nullptr;
            }

            auto& methodInfo = method->second;  //  Достаём параметры метода
            call->resolvedMethod = methodInfo;
            fieldCallee->resolvedMethod = methodInfo;
            appendMissingDefaultArgs(call, methodInfo->params, methodInfo->isVariadic);
            std::vector<std::shared_ptr<Type>> argTypes;
            for (size_t i = 0; i < call->args.size(); i++) {
                std::shared_ptr<Type> expectedArg = nullptr;
                if (i < methodInfo->params.size())
                    expectedArg = methodInfo->params[i].type; //  Для каждого параметра берём ожидаемый тип
                argTypes.push_back(analyzeExpr(call->args[i], expectedArg)); // Пробрасываем его для проверки
            }

            checkCallArguments("method '" + methodName + "'", methodInfo->params, argTypes, methodInfo->isVariadic, expr->line, expr->column);

            expr->resolvedType = methodInfo->returnType;
            return expr->resolvedType;
        }

        //  Прямой вызов по имени
        //  f(...)
        //  Namespace::f(...)
        //  A::B::f(...)
        std::shared_ptr<Symbol> sym = nullptr;
        std::string calleeName;

        if (auto* callee = dynamic_cast<Identifier*>(call->callee)) {
            calleeName = callee->name;
            sym = table.resolve(callee->name);
        }
        else if (auto* nsCallee = dynamic_cast<NamespaceAccess*>(call->callee)) {
            calleeName = nsCallee->nameSpace + "::" + nsCallee->member;
            sym = resolveNamespaceAccess(nsCallee);
        }
        else {
            analyzeExpr(call->callee);

            for (auto* arg : call->args) {
                analyzeExpr(arg);
            }

            error(expr->line, expr->column, "expression is not callable");
            return nullptr;
        }

        // Неявный вызов метода текущего класса:
        // class A { int f(){...} int g(){ return f(); } }
        if (!sym && currentClass) {
            auto methodIt = currentClass->methods.find(calleeName);

            if (methodIt != currentClass->methods.end()) {
                auto& methodInfo = methodIt->second;

                call->resolvedMethod = methodInfo;

                appendMissingDefaultArgs(call, methodInfo->params, methodInfo->isVariadic);

                std::vector<std::shared_ptr<Type>> argTypes;

                for (size_t i = 0; i < call->args.size(); i++) {
                    std::shared_ptr<Type> expectedArg = nullptr;

                    if (i < methodInfo->params.size()) {
                        expectedArg = methodInfo->params[i].type;
                    }

                    argTypes.push_back(analyzeExpr(call->args[i], expectedArg));
                }

                checkCallArguments("method '" + calleeName + "'", methodInfo->params, argTypes, methodInfo->isVariadic, expr->line, expr->column);

                expr->resolvedType = methodInfo->returnType;
                return expr->resolvedType;
            }
        }

        if (!sym) {
            error(expr->line, expr->column, "'" + calleeName + "' is not declared");
            return nullptr;
        } 

        //  Создание экземпляра класса: ClassName(args...).
        //  Если у класса есть конструктор — проверяем аргументы по его сигнатуре,
        //  иначе допускается только вызов без аргументов (default-конструктор по типам полей).
        if (sym->kind == SymbolKind::Class) {
            call->resolvedCallee = sym;
            std::vector<std::shared_ptr<Type>> argTypes;
            std::shared_ptr<FuncInfo> ctor = sym->classInfo ? sym->classInfo->constructor : nullptr;

            if (ctor) {
                appendMissingDefaultArgs(call, ctor->params, ctor->isVariadic);
            }

            for (size_t i = 0; i < call->args.size(); i++) {
                std::shared_ptr<Type> expectedArg = nullptr;
                if (ctor && i < ctor->params.size()) {
                    expectedArg = ctor->params[i].type;
                }
                argTypes.push_back(analyzeExpr(call->args[i], expectedArg));
            }

            if (ctor) {
                checkCallArguments("constructor of '" + calleeName + "'", ctor->params, argTypes, ctor->isVariadic, expr->line, expr->column);
            }
            else if (!argTypes.empty()) {
                error(expr->line, expr->column, "class '" + calleeName + "' has no constructor; default constructor takes no arguments");
            }

            expr->resolvedType = sym->type;
            return expr->resolvedType;
        }

        if (sym->kind != SymbolKind::Function) {
            error(expr->line, expr->column, "'" + calleeName + "' is not a function");
            return nullptr;
        }

        call->resolvedCallee = sym;

        if (sym->funcInfo && calleeName != "print" && calleeName != "len" && calleeName != "push" && calleeName != "pop" && calleeName != "input" && calleeName != "exit" && calleeName != "panic") {
            appendMissingDefaultArgs(call, sym->funcInfo->params, sym->funcInfo->isVariadic);
        }

        //  push(elem, arr)
        //  Важно: второй аргумент анализируем первым,
        //  чтобы получить тип элемента массива и пробросить его как expected
        //  в первый аргумент для контекстной типизации.
        if (calleeName == "push") {
            if (call->args.size() != 2) {
                error(expr->line, expr->column, "'push' expects 2 arguments (element, array), got " + std::to_string(call->args.size()));

                for (auto* arg : call->args) {
                    analyzeExpr(arg);
                }

                expr->resolvedType = makeType(TypeKind::Void);
                return expr->resolvedType;
            }

            auto arrType = analyzeExpr(call->args[1]);

            // проверка: второй аргумент должен быть lvalue и не const
            if (!isLvalue(call->args[1])) {
                error(expr->line, expr->column, "'push' second argument must be a mutable lvalue array");
            }
            else {
                auto sym = resolveTargetRoot(call->args[1]);
                if (sym && sym->isConst) {
                    error(expr->line, expr->column, "'push' cannot modify const array '" + sym->name + "'");
                }
            }

            if (!arrType || arrType->kind != TypeKind::DynArray) {
                error(expr->line, expr->column, "'push' second argument must be a dynamic array '[T]', got '" + typeToString(arrType) + "'");

                analyzeExpr(call->args[0]);
                expr->resolvedType = makeType(TypeKind::Void);
                return expr->resolvedType;
            }

            auto elemType = analyzeExpr(call->args[0], arrType->elementType);

            if (elemType && !isImplicitlyConvertible(elemType, arrType->elementType)) {
                error(expr->line, expr->column, "'push' element type mismatch: cannot convert '" + typeToString(elemType) + "' to '" + typeToString(arrType->elementType) + "'");
            }

            expr->resolvedType = makeType(TypeKind::Void);
            return expr->resolvedType;
        }

        
        //  Анализируем каждый аргумент и собираем их типы
        std::vector<std::shared_ptr<Type>> argTypes;

        for (size_t i = 0; i < call->args.size(); i++) {
            std::shared_ptr<Type> expectedArg = nullptr;

            if (sym->funcInfo && calleeName != "print" && calleeName != "len" && calleeName != "pop" && i < sym->funcInfo->params.size()) {
                expectedArg = sym->funcInfo->params[i].type;
            }

            argTypes.push_back(analyzeExpr(call->args[i], expectedArg));
        }

        //  input() имеет auto-возврат: тип берётся из контекста объявления,
        //  присваивания или return. Без контекста результат неоднозначен.
        if (calleeName == "input") {
            if (!argTypes.empty()) {
                error(expr->line, expr->column, "'input' expects 0 arguments, got " + std::to_string(argTypes.size()));
                expr->resolvedType = nullptr;
                return nullptr;
            }

            if (!expected) {
                error(expr->line, expr->column, "'input' requires an expected type");
                expr->resolvedType = nullptr;
                return nullptr;
            }

            if (!isInputSupportedType(expected)) {
                error(expr->line, expr->column, "'input' cannot read value of type '" + typeToString(expected) + "'");
                expr->resolvedType = nullptr;
                return nullptr;
            }

            expr->resolvedType = expected;
            return expr->resolvedType;
        }

        //  print(x1, x2, ...)
        if (calleeName == "print") {
            if (argTypes.empty()) {
                error(expr->line, expr->column, "'print' expects at least 1 argument, got 0");
            }

            for (size_t i = 0; i < argTypes.size(); i++) {
                if (!argTypes[i]) {
                    continue;
                }

                auto kind = argTypes[i]->kind;

                bool isSupported =
                    kind == TypeKind::Bool ||
                    kind == TypeKind::String ||
                    kind == TypeKind::Char ||
                    kind == TypeKind::Int8 ||
                    kind == TypeKind::Int16 ||
                    kind == TypeKind::Int32 ||
                    kind == TypeKind::Int64 ||
                    kind == TypeKind::Uint8 ||
                    kind == TypeKind::Uint16 ||
                    kind == TypeKind::Uint32 ||
                    kind == TypeKind::Uint64 ||
                    kind == TypeKind::Float32 ||
                    kind == TypeKind::Float64 ||
                    kind == TypeKind::Array ||
                    kind == TypeKind::DynArray;

                if (!isSupported) {
                    error(expr->line, expr->column, "'print' argument " + std::to_string(i + 1) + " has unsupported type '" + typeToString(argTypes[i]) + "'");
                }
            }

            expr->resolvedType = makeType(TypeKind::Void);
            return expr->resolvedType;
        }

        //  len(x)
        if (calleeName == "len") {
            if (argTypes.size() != 1) {
                error(expr->line, expr->column, "'len' expects 1 argument, got " + std::to_string(argTypes.size()));
            }
            else if (argTypes[0]) {
                auto kind = argTypes[0]->kind;

                if (kind != TypeKind::Array && kind != TypeKind::DynArray && kind != TypeKind::String) {
                    error(expr->line, expr->column, "'len' argument must be an array or string, got '" + typeToString(argTypes[0]) + "'");
                }
            }

            expr->resolvedType = makeType(TypeKind::Int32);
            return expr->resolvedType;
        }


        //  pop(arr)
        if (calleeName == "pop") {
            if (argTypes.size() != 1) {
                error(expr->line, expr->column, "'pop' expects 1 argument (array), got " + std::to_string(argTypes.size()));
                return nullptr;
            }
            // проверка: аргумент должен быть lvalue и не const
            if (!isLvalue(call->args[0])) {
                error(expr->line, expr->column, "'pop' argument must be a mutable lvalue array");
            }
            else {
                auto sym = resolveTargetRoot(call->args[0]);
                if (sym && sym->isConst) {
                    error(expr->line, expr->column, "'pop' cannot modify const array '" + sym->name + "'");
                }
            }

            if (argTypes[0] && argTypes[0]->kind != TypeKind::DynArray) {
                error(expr->line, expr->column, "'pop' argument must be a dynamic array '[T]', got '" + typeToString(argTypes[0]) + "'");
                return nullptr;
            }

            if (argTypes[0]) {
                expr->resolvedType = argTypes[0]->elementType;
            }
            else {
                expr->resolvedType = nullptr;
            }

            return expr->resolvedType;
        }

        //  Проверяем обычные функции (для extern-C включается маршалинг string/array/ptr -> uint64)
        if (sym->funcInfo) {
            checkCallArguments("function '" + calleeName + "'", sym->funcInfo->params, argTypes, sym->funcInfo->isVariadic, expr->line, expr->column, sym->funcInfo->isExternC);
        }

        //  Пробрасываем флаги в AST для кодгена
        if (sym->funcInfo) {
            call->isExternC = sym->funcInfo->isExternC;
            call->isVariadic = sym->funcInfo->isVariadic;
            expr->resolvedType = sym->funcInfo->returnType;
        }
        else {
            expr->resolvedType = sym->type;
        }

        return expr->resolvedType; 
    }

    //  Доступ к полю структуры/класса (p.x)
    if (auto* field = dynamic_cast<FieldAccess*>(expr)) {
        // Это не доступ к полю объекта, а чтение default-значения поля типа.
        if (auto* id = dynamic_cast<Identifier*>(field->object)) {
            auto typeSym = table.resolve(id->name);

            if (typeSym && (typeSym->kind == SymbolKind::Struct || typeSym->kind == SymbolKind::Class)) {

                FieldInfo* found = findFieldInTypeSymbol(typeSym, field->field);

                if (!found) {
                    error(expr->line, expr->column, "type '" + id->name + "' has no default field '" + field->field + "'");
                    return nullptr;
                }

                field->resolvedField = found;
                field->isTypeDefaultFieldAccess = true;
                expr->resolvedType = found->type;
                return expr->resolvedType;
            }
        }

        // Спец-форма: Namespace::TypeName.field
        if (auto* ns = dynamic_cast<NamespaceAccess*>(field->object)) {
            auto typeSym = resolveNamespaceAccess(ns);
            std::string fullTypeName = ns->nameSpace + "::" + ns->member;

            if (typeSym &&
                (typeSym->kind == SymbolKind::Struct || typeSym->kind == SymbolKind::Class)) {

                FieldInfo* found = findFieldInTypeSymbol(typeSym, field->field);

                if (!found) {
                    error(expr->line, expr->column, "type '" + fullTypeName + "' has no default field '" + field->field + "'");
                    return nullptr;
                }

                field->resolvedField = found;
                field->isTypeDefaultFieldAccess = true;
                expr->resolvedType = found->type;
                return expr->resolvedType;
            }
        }
        auto objType = analyzeExpr(field->object);  //  Откуда мы пытаемся взять поле
        if (!objType) return nullptr;

        if (objType->kind == TypeKind::Struct) {    //  Если структура
            auto structSym = resolveQualifiedSymbol(objType->name);  //  Ищем в таблице
            if (structSym && structSym->structInfo) {   //  Проходимся по вектору пар: имя -> тип поля
                for (auto& f : structSym->structInfo->fields) {
                    if (f.name == field->field) {    //  Нашли поле
                        field->resolvedField = &f;
                        expr->resolvedType = f.type;
                        return expr->resolvedType;
                    }
                }
                error(expr->line, expr->column, "struct '" + objType->name + "' has no field '" + field->field + "'");
            }   //  Не нашли поле
            return nullptr;
        }

        if (objType->kind == TypeKind::Class) {     //  Если класс
            auto classSym = resolveQualifiedSymbol(objType->name);
            if (classSym && classSym->classInfo) {      //  Точно также ищем поля как в структуре
                for (auto& f : classSym->classInfo->fields) {
                    if (f.name == field->field) {
                        field->resolvedField = &f;
                        expr->resolvedType = f.type;
                        return expr->resolvedType;
                    }
                }   //  Если такого поля не нашли значит это возможно метод

                auto method = classSym->classInfo->methods.find(field->field);  //  Ищем метод
                if (method != classSym->classInfo->methods.end()) {
                    error(expr->line, expr->column, "method '" + objType->name + "." + field->field + "' is not a value; call it with '()'"); 
                    return nullptr;
                }
                error(expr->line, expr->column, "class '" + objType->name + "' has no field or method '" + field->field + "'");
            }
            return nullptr;
        }

        error(expr->line, expr->column, "field access on non-struct/class type '" + typeToString(objType) + "'");
        return nullptr;
    }

    //  Индексация массива arr[i]
    if (auto* arr = dynamic_cast<ArrayAccess*>(expr)) {
        auto objType = analyzeExpr(arr->object);    //  Здесь либо массив, либо строка
        auto indexType = analyzeExpr(arr->index);   //  Здесь число
        if (!objType) return nullptr;

        if (objType->kind != TypeKind::Array && objType->kind != TypeKind::DynArray && objType->kind != TypeKind::String) {
            error(expr->line, expr->column, "index operator on non-array type '" + typeToString(objType) + "'");
            return nullptr;
        }

        if (indexType && !isIntType(indexType)) {   //  Индекс должен быть целым
            error(expr->line, expr->column, "array index must be integer, got '" + typeToString(indexType) + "'");
        }

        if (objType->kind == TypeKind::String)
            expr->resolvedType = makeType(TypeKind::Char);  //  Элемент строки -> char
        else
            expr->resolvedType = objType->elementType;  //  Элемент массива -> определяем
        return expr->resolvedType;
    }

    //  Литерал массива [1, 2, 3]. Если контекст задаёт тип элемента (float[3] arr = [3.14, 5, 6.2]),
    //  то он становится эталоном и каждый элемент неявно приводится к нему (5 → 5.0).
    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(expr)) {
        std::shared_ptr<Type> targetElemType = nullptr;

        bool expectedArray = expected && (expected->kind == TypeKind::Array || expected->kind == TypeKind::DynArray);

        if (expectedArray) {
            targetElemType = expected->elementType;
        }

        // Пустой массив: тип можно взять только из контекста.
        if (arrLit->elements.empty()) {
            if (!expectedArray || !targetElemType) {
                error(expr->line, expr->column, "cannot infer type of empty array literal");
                return nullptr;
            }

            // [] в контексте динамического массива: string[] names = [];
            if (expected->kind == TypeKind::DynArray) {
                expr->resolvedType = makeDynArrayType(targetElemType);
                return expr->resolvedType;
            }
        }

        // Непустой массив без контекста:
        // auto arr = [1, 2, 3] -> int[]
        if (!targetElemType) {
            targetElemType = analyzeExpr(arrLit->elements[0]);

            if (!targetElemType) {
                return nullptr;
            }

            for (size_t j = 1; j < arrLit->elements.size(); ++j) {
                auto elemType = analyzeExpr(arrLit->elements[j]);

                if (!elemType) {
                    return nullptr;
                }

                auto merged = commonType(targetElemType, elemType);

                if (!merged) {
                    error(arrLit->elements[j]->line, arrLit->elements[j]->column, "array element " + std::to_string(j + 1) + ": incompatible types '" + typeToString(targetElemType) + "' and '" + typeToString(elemType) + "'");
                    return nullptr;
                }

                targetElemType = merged;
            }
        }

        // Проверяем все элементы относительно targetElemType.
        for (size_t j = 0; j < arrLit->elements.size(); ++j) {
            auto elemType = analyzeExpr(arrLit->elements[j], targetElemType);

            if (elemType && targetElemType && !isImplicitlyConvertible(elemType, targetElemType)) {
                error(arrLit->elements[j]->line, arrLit->elements[j]->column, "array element " + std::to_string(j + 1) + ": cannot convert '" + typeToString(elemType) + "' to '" + typeToString(targetElemType) + "'");
            }

            if (targetElemType) {
                arrLit->elements[j]->resolvedType = targetElemType;
            }
        }

        // Если есть expected int[] — литерал становится динамическим массивом.
        if (expected && expected->kind == TypeKind::DynArray) {
            expr->resolvedType = makeDynArrayType(targetElemType);
            return expr->resolvedType;
        }

        // Если есть expected int[3] — литерал становится фиксированным массивом.
        if (expected && expected->kind == TypeKind::Array) {
            int literalSize = static_cast<int>(arrLit->elements.size());

            if (literalSize != expected->arraySize) {
                error(expr->line, expr->column, "array literal size mismatch: expected " + std::to_string(expected->arraySize) + " elements, got " + std::to_string(literalSize));
                return nullptr;
            }

            expr->resolvedType = expected;
            return expr->resolvedType;
        }

        // Без expected:
        // auto arr = [1, 2, 3] -> int[]
        // auto matrix = [[1, 2], [3, 4]] -> int[][]
        expr->resolvedType = makeDynArrayType(targetElemType);
        return expr->resolvedType;
    } 

    //  Литерал структуры Point { x: 5, y: 10 } 
    if (auto* structLit = dynamic_cast<StructLiteral*>(expr)) {
        auto sym = resolveQualifiedSymbol(structLit->name);
        if (!sym || sym->kind != SymbolKind::Struct) {
            error(expr->line, expr->column, "'" + structLit->name + "' is not a struct type");
            return nullptr;
        }

        if (sym->structInfo) {
            std::unordered_set<std::string> seen;
            for (auto& init : structLit->fields) {  //  Проверяем каждое инициализируемое поле
                //  Двойное определение одного поля: Point{x: 1, x: 2}.
                if (!seen.insert(init.name).second) {
                    error(expr->line, expr->column, "field '" + init.name + "' of '" + structLit->name + "' is already initialized");
                    analyzeExpr(init.value);    //  всё равно прогоняем для сбора возможных ошибок в выражении
                    continue;
                }

                const FieldInfo* foundField = nullptr;
                for (auto& f : sym->structInfo->fields) {
                    if (f.name == init.name) { foundField = &f; break; }
                }

                //  Инициализация несуществующего поля: Point{unknownField: ...}.
                if (!foundField) {
                    error(expr->line, expr->column, "struct '" + structLit->name + "' has no field '" + init.name + "'");
                    analyzeExpr(init.value);
                    continue;
                }

                //  Тип поля прокидываем как expected для контекстной типизации литералов
                //  и проверяем совместимость через widening (isImplicitlyConvertible).
                auto valType = analyzeExpr(init.value, foundField->type);
                if (valType && foundField->type && !isImplicitlyConvertible(valType, foundField->type)) {
                    error(expr->line, expr->column, "field '" + init.name + "' of '" + structLit->name + "': cannot convert '" + typeToString(valType) + "' to '" + typeToString(foundField->type) + "'");
                }
            }
            // Не достраиваем отсутствующие поля на этапе semantic analysis:
            // StructLiteral в codegen сначала копирует runtime default-слоты,
            // затем накладывает явно указанные поля. Так `Type.field = value`
            // влияет только на последующие runtime-инициализации.
        }

        auto type = makeType(TypeKind::Struct);     //  Результат — тип этой структуры
        type->name = sym->type ? sym->type->name : structLit->name;
        expr->resolvedType = type;
        return expr->resolvedType;
    }

    //  Явное приведение типа cast<T>(expr) 
    if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        auto fromType = analyzeExpr(cast->value);   //  Начальный тип
        auto targetType = resolveTypeName(cast->targetType);    //  Нужный тип
        if (!targetType) {
            error(expr->line, expr->column, "unknown type '" + typeNameToString(cast->targetType) + "' in cast");
            return nullptr;
        }
        if (fromType && !isCastable(fromType, targetType)) {
            error(expr->line, expr->column, "cannot cast '" + typeToString(fromType) + "' to '" + typeToString(targetType) + "'");
        }
        expr->resolvedType = targetType;
        return expr->resolvedType;
    }

    //  Доступ через namespace std::expected
    if (auto* namespase = dynamic_cast<NamespaceAccess*>(expr)) {
        auto sym = resolveNamespaceAccess(namespase);

        std::string fullName = namespase->nameSpace + "::" + namespase->member;

        if (!sym) {
            error(expr->line, expr->column, "'" + namespase->member + "' is not declared in namespace '" + namespase->nameSpace + "'");
            return nullptr;
        }

        if (sym->kind != SymbolKind::Variable) {
            error(expr->line, expr->column, nonValueSymbolMessage(sym, fullName));
            return nullptr;
        }
        
        auto knownType = ensureVariableTypeKnown(sym, expr->line, expr->column);
        if (!knownType) {
            return nullptr;
        }

        namespase->resolvedSym = sym;
        expr->resolvedType = knownType;
        return expr->resolvedType;
    }

    return nullptr;
}

static bool alwaysReturns(Stmt* stmt) {
    if (!stmt) return false;

    // return ...;
    if (dynamic_cast<Return*>(stmt)) {
        return true;
    }

    // { stmt1; stmt2; ... }
    // Блок гарантированно возвращает, если какая-то инструкция внутри него
    // гарантированно возвращает. Всё после неё уже недостижимо.
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto* inner : block->statements) {
            if (alwaysReturns(inner)) {
                return true;
            }
        }
        return false;
    }

    // if (...) { ... } else { ... }
    // Возврат гарантирован только если есть else и обе ветки возвращают.
    if (auto* ifStmt = dynamic_cast<If*>(stmt)) {
        if (!ifStmt->elseBranch) {
            return false;
        }

        return alwaysReturns(ifStmt->thenBranch) && alwaysReturns(ifStmt->elseBranch);
    }

    return false;
}

void SemanticAnalyzer::checkDuplicateParams(const std::vector<Param>& params, int line, int column,const std::string& where) {
    std::unordered_set<std::string> seen;

    for (const auto& param : params) {
        if (!seen.insert(param.name).second) {
            error(line, column, "duplicate parameter '" + param.name + "' in " + where);
        }
    }
}

void SemanticAnalyzer::checkDuplicateFields(const std::vector<StructField>& fields, int line, int column, const std::string& where) {
    std::unordered_set<std::string> seen;

    for (const auto& field : fields) {
        if (!seen.insert(field.name).second) {
            error(line, column, "duplicate field '" + field.name + "' in " + where);
        }
    }
}

void SemanticAnalyzer::checkDuplicateMethods(const std::vector<FuncDecl*>& methods, int line, int column, const std::string& className) {
    std::unordered_set<std::string> seen;

    for (auto* method : methods) {
        if (!method) continue;

        if (!seen.insert(method->name).second) {
            error(line, column, "duplicate method '" + method->name + "' in class '" + className + "'");
        }
    }
}

void SemanticAnalyzer::checkDuplicateNestedStructs(const std::vector<StructDecl*>& structs, int line, int column, const std::string& className) {
    std::unordered_set<std::string> seen;

    for (auto* strukt : structs) {
        if (!strukt) continue;

        if (!seen.insert(strukt->name).second) {
            error(line, column, "duplicate nested struct '" + strukt->name + "' in class '" + className + "'");
        }
    }
}

static const char* symbolKindName(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Variable:  return "variable";
        case SymbolKind::Function:  return "function";
        case SymbolKind::Struct:    return "struct";
        case SymbolKind::Class:     return "class";
        case SymbolKind::TypeAlias: return "type alias";
        case SymbolKind::Namespace: return "namespace";
    }

    return "symbol";
}

bool SemanticAnalyzer::declareTopLevelSymbol(std::shared_ptr<Symbol> sym, Stmt* decl, bool allowNamespaceMerge) {
    if (!sym) return false;

    auto existing = table.resolveCurrentScope(sym->name);   //  Смотрим есть ли в общем Scope такой элемент

    if (existing) {
        if (allowNamespaceMerge && existing->kind == SymbolKind::Namespace) {
            if (!existing->namespaceScope) {
                existing->namespaceScope = std::make_shared<Scope>();
            }
            sym->namespaceScope = existing->namespaceScope; //  Разрешаем разным объявлениям namespace ссылаться на один общий Scope
            return true;                                    //  То есть объединяем namespace
        }

        invalidTopLevelDecls.insert(decl);  //  Все остальные дубли в глобальном Scope -> ошибка
        error(decl->line, decl->column, "duplicate top-level name '" + sym->name + "': " + std::string(symbolKindName(existing->kind)) + " is already declared in this scope");
        return false;
    }

    table.declare(sym); //  Спокойно добавляем в таблицу если не нашли символ
    return true;
}

//  Первый проход — сбор top-level имён 
//  Обходим объявления верхнего уровня и регистрируем имена функций, структур,
//  алиасов и namespace в таблице символов до анализа их тел.
//  Это позволяет функции main вызывать функцию add, объявленную ниже.
void SemanticAnalyzer::predeclareTopLevel(const std::vector<Stmt*>& decls) {
    for (auto* decl : decls) {
        if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
            predeclareTopLevel({ exp->decl });  //  Рекурсивно обрабатываем export декларацию
            continue;
        }

        if (auto* func = dynamic_cast<FuncDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = func->name;
            sym->kind = SymbolKind::Function;
            sym->funcInfo = std::make_shared<FuncInfo>();   //  Здесь мы пока что не записываем сигнатуру, заполним её на следующих проходах
            declareTopLevelSymbol(sym, decl);
        }
        else if (auto* strukt = dynamic_cast<StructDecl*>(decl)) {
            std::string qualifiedName = appendQualifiedName(currentNamespace, strukt->name);
            auto sym = std::make_shared<Symbol>();
            sym->name = strukt->name;
            sym->kind = SymbolKind::Struct;

            sym->structInfo = std::make_shared<StructInfo>();
            sym->structInfo->name = qualifiedName;

            auto type = makeType(TypeKind::Struct);
            type->name = qualifiedName;
            sym->type = type;

            declareTopLevelSymbol(sym, decl);
        }
        else if (auto* clas = dynamic_cast<ClassDecl*>(decl)) {
            std::string qualifiedName = appendQualifiedName(currentNamespace, clas->name);  
            auto sym = std::make_shared<Symbol>();
            sym->name = clas->name;
            sym->kind = SymbolKind::Class;

            sym->classInfo = std::make_shared<ClassInfo>();
            sym->classInfo->name = qualifiedName;

            auto type = makeType(TypeKind::Class);
            type->name = qualifiedName;
            sym->type = type;

            declareTopLevelSymbol(sym, decl);
        }
        else if (auto* alias = dynamic_cast<TypeAlias*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = alias->alias;
            sym->kind = SymbolKind::TypeAlias;
            sym->type = nullptr;

            // Для ленивого разрешения, когда у нас синоним ещё не создан, но обращение к нему уже есть
            sym->aliasTarget = alias->original;
            sym->isResolvingAlias = false;

            declareTopLevelSymbol(sym, decl);
        }
        else if (auto* namespase = dynamic_cast<NamespaceDecl*>(decl)) {
            auto sym = std::make_shared<Symbol>();
            sym->name = namespase->name;
            sym->kind = SymbolKind::Namespace;
            sym->namespaceScope = std::make_shared<Scope>();

            if (!declareTopLevelSymbol(sym, decl, true)) {  
                continue;   //  True говорит о том что это namespace, а значит его можно попытаться объединить с уже существующим
            }

            table.pushScope(sym->namespaceScope);   //  Закидываем в текущий scope новую область
            std::string prevNamespace = currentNamespace;   //  Переключаем текущую область
            currentNamespace = appendQualifiedName(currentNamespace, namespase->name);
            predeclareTopLevel(namespase->decls);   //  Рекурсивно обрабатываем вложенный scope как и текущий
            currentNamespace = prevNamespace;   // И возвращаемся
            table.exitScope();
        }  
        else if (auto* var = dynamic_cast<VarDecl*>(decl)) {
            for (auto* v : var->vars) {
                auto sym = std::make_shared<Symbol>();
                sym->name = v->name;
                sym->kind = SymbolKind::Variable;
                sym->isConst = var->isConst;
                sym->isInitialized = false;
                sym->type = nullptr;
            
                declareTopLevelSymbol(sym, decl);
            }
        }
    }
}

void SemanticAnalyzer::collectTopLevel(const std::vector<Stmt*>& decls) {
    for (auto* decl : decls) {  
        if (invalidTopLevelDecls.count(decl)) {
            continue;
        }

        if (auto* func = dynamic_cast<FuncDecl*>(decl)) {   //  Функция
            auto sym = std::make_shared<Symbol>();
            checkDuplicateParams(func->params, decl->line, decl->column, "function '" + func->name + "'");
            sym->name = func->name;
            sym->kind = SymbolKind::Function;
            auto returnType = resolveTypeName(func->returnType);

            if (!returnType) {
                error(decl->line, decl->column, "unknown return type '" + typeNameToString(func->returnType) + "' in function '" + func->name + "'");
            }

            sym->type = returnType;
            sym->funcInfo = std::make_shared<FuncInfo>();   //  Заполняем информацию о сигнатуре
            sym->funcInfo->returnType = sym->type;
            for (auto& param : func->params) {
                auto paramType = resolveDeclaredType(param.isAuto, param.isConst, param.typeName, param.defaultValue,
                    decl->line, decl->column, "parameter '" + param.name + "' of function '" + func->name + "'", DeclContext::Parameter);
                sym->funcInfo->params.push_back(makeParamInfo(param, paramType));
            }
            func->resolvedSym = sym;
            func->resolvedInfo = sym->funcInfo;

            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, decl->column, result.error());
            }
        }
        
        else if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {  //  Структура
            std::string qualifiedName = appendQualifiedName(currentNamespace, structDecl->name);
            auto sym = std::make_shared<Symbol>();
            sym->name = structDecl->name;
            sym->kind = SymbolKind::Struct;

            sym->structInfo = std::make_shared<StructInfo>();   //  Заполняем информацию о полях с их типами
            sym->structInfo->name = qualifiedName;
            checkDuplicateFields(structDecl->fields, decl->line, decl->column, "struct '" + structDecl->name + "'");
            for (auto& field : structDecl->fields) {
                auto fieldType = resolveDeclaredType(field.isAuto, field.isConst, field.typeName, field.defaultValue,
                    decl->line, decl->column, "field '" + field.name + "' of struct '" + structDecl->name + "'", DeclContext::Field);
                field.resolvedType = fieldType;
                sym->structInfo->fields.push_back({field.name, fieldType, field.isConst, field.defaultValue});
            }

            auto type = makeType(TypeKind::Struct);    //  Тип самой структуры — Struct с именем
            type->name = qualifiedName;
            sym->type = type;

            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, decl->column, result.error());
            }
        }
        else if (auto* alias = dynamic_cast<TypeAlias*>(decl)) {
            auto sym = table.resolveCurrentScope(alias->alias);

            if (!sym || sym->kind != SymbolKind::TypeAlias) {
                sym = std::make_shared<Symbol>();
                sym->name = alias->alias;
                sym->kind = SymbolKind::TypeAlias;

                auto result = table.declare(sym);
                if (!result) {
                    error(decl->line, decl->column, result.error());
                    continue;
                }
            }

            sym->aliasTarget = alias->original;
        }
        else if (auto* namespase = dynamic_cast<NamespaceDecl*>(decl)) {
            auto sym = table.resolveCurrentScope(namespase->name);

            if (!sym || sym->kind != SymbolKind::Namespace) {
                error(decl->line, decl->column, "internal error: namespace '" + namespase->name + "' was not predeclared");
                continue;
            }

            if (!sym->namespaceScope) {
                sym->namespaceScope = std::make_shared<Scope>();
            }

            table.pushScope(sym->namespaceScope);
            std::string prevNamespace = currentNamespace;
            currentNamespace = appendQualifiedName(currentNamespace, namespase->name);
            collectTopLevel(namespase->decls);
            currentNamespace = prevNamespace;
            table.exitScope();
        }   
        else if (auto* clas = dynamic_cast<ClassDecl*>(decl)) {  //  Класс
            std::string qualifiedName = appendQualifiedName(currentNamespace, clas->name);
            auto sym = std::make_shared<Symbol>();
            sym->name = clas->name;
            sym->kind = SymbolKind::Class;

            sym->classInfo = std::make_shared<ClassInfo>();
            sym->classInfo->name = qualifiedName;

            checkDuplicateFields(clas->fields, decl->line, decl->column, "class '" + clas->name + "'");
            checkDuplicateMethods(clas->methods, decl->line, decl->column, clas->name);
            checkDuplicateNestedStructs(clas->structs, decl->line, decl->column, clas->name);

            for (auto& field : clas->fields) {  //  Поля класса
                auto fieldType = resolveDeclaredType(field.isAuto, field.isConst, field.typeName, field.defaultValue,
                    decl->line, decl->column, "field '" + field.name + "' of class '" + clas->name + "'", DeclContext::Field);
                field.resolvedType = fieldType;
                sym->classInfo->fields.push_back({field.name, fieldType, field.isConst, field.defaultValue});
            }

            //  Вложенные структуры должны быть известны до сигнатур методов:
            //  class A { struct S {...} S makeS() {...} }.
            for (auto* nested : clas->structs) {
                if (!nested) continue;
                auto nestedInfo = std::make_shared<StructInfo>();
                nestedInfo->name = qualifiedName + "::" + nested->name;
                checkDuplicateFields(nested->fields, nested->line, nested->column, "nested struct '" + qualifiedName + "::" + nested->name + "'");
                for (auto& field : nested->fields) {
                    auto fieldType = resolveDeclaredType(field.isAuto, field.isConst, field.typeName, field.defaultValue,
                        decl->line, decl->column,
                        "field '" + field.name + "' of nested struct '" + nestedInfo->name + "'", DeclContext::Field);
                    field.resolvedType = fieldType;
                    nestedInfo->fields.push_back({field.name, fieldType, field.isConst, field.defaultValue});
                }
                sym->classInfo->nestedStructs[nested->name] = nestedInfo;
            }

            auto prevClass = currentClass;
            currentClass = sym->classInfo;

            for (auto* method : clas->methods) { //  Методы класса
                auto methodInfo = std::make_shared<FuncInfo>();
                auto returnType = resolveTypeName(method->returnType);

                if (!returnType) {
                    error(method->line, method->column, "unknown return type '" + typeNameToString(method->returnType) + "' in method '" + clas->name + "." + method->name + "'");
                }

                methodInfo->returnType = returnType;
                checkDuplicateParams(method->params, method->line, method->column, "method '" + clas->name + "." + method->name + "'");
                for (auto& param : method->params) {
                    auto paramType = resolveDeclaredType(param.isAuto, param.isConst, param.typeName, param.defaultValue,
                        decl->line, decl->column,
                        "parameter '" + param.name + "' of method '" + clas->name + "." + method->name + "'", DeclContext::Parameter);
                    methodInfo->params.push_back(makeParamInfo(param, paramType));
                }
                method->resolvedInfo = methodInfo;
                sym->classInfo->methods[method->name] = methodInfo;
            }

            if (clas->constructor) {     //  Конструктор
                auto classInfo = std::make_shared<FuncInfo>();
                classInfo->returnType = makeType(TypeKind::Void);
                checkDuplicateParams( clas->constructor->params, clas->constructor->line, clas->constructor->column, "constructor of class '" + clas->name + "'");
                for (auto& param : clas->constructor->params) { //  Параметры конструктора
                    auto paramType = resolveDeclaredType(param.isAuto, param.isConst, param.typeName, param.defaultValue,
                        decl->line, decl->column,
                        "parameter '" + param.name + "' of constructor of '" + clas->name + "'", DeclContext::Parameter);
                    classInfo->params.push_back(makeParamInfo(param, paramType));
                }
                clas->constructor->resolvedInfo = classInfo;
                sym->classInfo->constructor = classInfo;
            }

            //  Деструктор. По спецификации без параметров и возвращаемого типа.
            if (clas->destructor) {
                auto dtorInfo = std::make_shared<FuncInfo>();
                dtorInfo->returnType = makeType(TypeKind::Void);
                if (!clas->destructor->params.empty()) {
                    error(decl->line, decl->column, "destructor of '" + clas->name + "' must take no parameters");
                }
                clas->destructor->resolvedInfo = dtorInfo;
                sym->classInfo->destructor = dtorInfo;
            }

            currentClass = prevClass;

            auto type = makeType(TypeKind::Class);
            type->name = qualifiedName;
            sym->type = type;

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, decl->column, result.error());
        }
        else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) { //  Export 
            std::vector<Stmt*> inner;
            inner.push_back(exp->decl);     //  Закидываем объявление идущее на экспорт
            collectTopLevel(inner);

            std::string exportedName;   //  Определяем имя объявленного символа и помечаем его exported
            if (auto* func = dynamic_cast<FuncDecl*>(exp->decl))       exportedName = func->name;
            else if (auto* strukt = dynamic_cast<StructDecl*>(exp->decl)) exportedName = strukt->name;
            else if (auto* clas = dynamic_cast<ClassDecl*>(exp->decl))  exportedName = clas->name;
            else if (auto* alias = dynamic_cast<TypeAlias*>(exp->decl))  exportedName = alias->alias;
            else if (auto* namespase = dynamic_cast<NamespaceDecl*>(exp->decl)) exportedName = namespase->name;
            else if (auto* var = dynamic_cast<VarDecl*>(exp->decl)) {
                //  VarDecl содержит несколько переменных — помечаем каждую
                for (auto* v : var->vars) {
                    auto sym = table.resolve(v->name);
                    if (sym) sym->isExported = true;
                }
            }

            if (!exportedName.empty()) {
                auto sym = table.resolve(exportedName);
                if (sym) sym->isExported = true;
            }
        }

        else if (auto* var = dynamic_cast<VarDecl*>(decl)) {
            std::shared_ptr<Type> declaredType = nullptr;

            if (!var->isAuto) {
                declaredType = resolveTypeName(var->typeName);
                if (!declaredType) {
                    error(decl->line, decl->column, "unknown type '" + typeNameToString(var->typeName) + "'");
                }
                else if (declaredType->kind == TypeKind::Void) {
                    error(decl->line, decl->column, "cannot declare variable with type 'void'");
                }
            }

            for (auto* v : var->vars) {
                auto sym = table.resolveCurrentScope(v->name);

                if (!sym || sym->kind != SymbolKind::Variable) {
                    sym = std::make_shared<Symbol>();
                    sym->name = v->name;
                    sym->kind = SymbolKind::Variable;

                    auto result = table.declare(sym);
                    if (!result) {
                        error(decl->line, decl->column, result.error());
                        continue;
                    }
                }
                v->resolvedSym = sym;
                sym->isConst = var->isConst;
                sym->isInitialized = (v->init != nullptr);
                sym->isAuto = var->isAuto;
                sym->autoInit = var->isAuto ? v->init : nullptr;

                if (var->isAuto) {
                    sym->type = nullptr;

                    if (!v->init) {
                        error(decl->line, decl->column, "'auto' variable '" + v->name + "' requires initializer");
                    }
                }
                else {
                    sym->type = declaredType;
                } 
            }
        }
    }
}

void SemanticAnalyzer::analyzeBlock(Block* block) {
    table.enterScope();
    for (auto* stmt : block->statements)   //  Смотрим стэйтменты внутри текущей области
        analyzeStmt(stmt);
    table.exitScope();
}



void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (invalidTopLevelDecls.count(stmt)) {
        return;
    }

    if (auto* var = dynamic_cast<VarDecl*>(stmt)) { //  Если это объявление переменной
        //  Тип общий на весь список переменных (для не-auto)
        std::shared_ptr<Type> declaredType;
        bool isDynArrayDecl = false;
        if (!var->isAuto) {
            declaredType = resolveTypeName(var->typeName);
            if (!declaredType) {
                error(stmt->line, stmt->column, "unknown type '" + typeNameToString(var->typeName) + "'");
            }
            else if (declaredType->kind == TypeKind::Void) {
                error(stmt->line, stmt->column, "cannot declare variable with type 'void'");
            }
            //  DynArray без инициализатора считается пустым массивом
            if (var->typeName && !var->typeName->suffixes.empty() && var->typeName->suffixes.back().isDynamic) {
                isDynArrayDecl = true;
            }
        }

        for (auto* v : var->vars) {
            auto sym = table.resolveCurrentScope(v->name);
            bool alreadyDeclared = false;

            if (sym && sym->kind == SymbolKind::Variable) {
                alreadyDeclared = true;
            }
            else {
                sym = std::make_shared<Symbol>();
                sym->name = v->name;
                sym->kind = SymbolKind::Variable;
            }

            sym->isConst = var->isConst;
            sym->isInitialized = (v->init != nullptr);
            
            if (!sym->isInitialized && isDynArrayDecl)
                sym->isInitialized = true;

            if (var->isAuto) {  //  auto — выводим тип из инициализатора
                std::shared_ptr<Type> initType = analyzeExpr(v->init);
                if (!initType) {
                    error(stmt->line, stmt->column, "'auto' requires initializer to infer type for '" + v->name + "'");
                }
                sym->type = initType;
                sym->intConstValue = std::nullopt;

                if (var->isConst && v->init && sym->type && isIntType(sym->type)) {
                    auto value = evalConstIntExpr(v->init);

                    if (value.has_value()) {
                        sym->intConstValue = *value;
                    }
                }
            }
            else {
                sym->type = declaredType;

                if (!v->init && var->isConst) {
                    //  Константа без явного инициализатора запрещена.
                    error(stmt->line, stmt->column, "const variable '" + v->name + "' requires an explicit initializer");
                }
                else if (!v->init && declaredType && declaredType->kind != TypeKind::Void) {
                    v->init = makeDefaultExprForType(declaredType, stmt->line, stmt->column);
                    sym->isInitialized = true;
                }

                std::shared_ptr<Type> initType = nullptr;
                if (v->init) {
                    initType = analyzeExpr(v->init, sym->type);
                }

                if (initType && sym->type) {
                    if (!isImplicitlyConvertible(initType, sym->type)) {
                        error(stmt->line, stmt->column, "cannot initialize '" + v->name + "' of type '" + typeToString(sym->type) + "' with '" + typeToString(initType) + "'");
                    }
                }

                sym->intConstValue = std::nullopt;

                if (var->isConst && v->init && sym->type && isIntType(sym->type)) {
                    auto value = evalConstIntExpr(v->init);

                    if (value.has_value()) {
                        sym->intConstValue = *value;
                    }
                }
            }

            if (!alreadyDeclared) {
                auto result = table.declare(sym);
                if (!result) {
                    error(stmt->line, stmt->column, result.error());
                }
            }
            v->resolvedSym = sym; 
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {    //  Если это блок то соответственно анализируем блок
        analyzeBlock(block);
    }
    else if (auto* assign = dynamic_cast<Assign*>(stmt)) {  //  Присваивание, проверяем типы, const, инициализацию
        //  Спец-форма: переопределение default-значения поля типа `TypeName.field = value`.
        //  Применимо к struct и class. Обрабатываем отдельно, чтобы обычная ветка анализа
        //  Identifier не ругалась на использование имени типа как значения.
        if (auto* fa = dynamic_cast<FieldAccess*>(assign->target)) {
            std::shared_ptr<Symbol> typeSym = nullptr;
            std::string typeNameForError;

            if (auto* id = dynamic_cast<Identifier*>(fa->object)) {
                typeSym = table.resolve(id->name);
                typeNameForError = id->name;
            }
            else if (auto* ns = dynamic_cast<NamespaceAccess*>(fa->object)) {
                typeSym = resolveNamespaceAccess(ns);
                typeNameForError = ns->nameSpace + "::" + ns->member;
            }

            bool isType = typeSym &&
                (typeSym->kind == SymbolKind::Struct || typeSym->kind == SymbolKind::Class);

            if (isType) {
                FieldInfo* field = findFieldInTypeSymbol(typeSym, fa->field);

                if (!field) {
                    const char* what = typeSym->kind == SymbolKind::Struct ? "struct" : "class";
                    error(stmt->line, stmt->column, std::string(what) + " '" + typeNameForError + "' has no field '" + fa->field + "'");
                    return;
                }

                if (field->isConst) {
                    error(stmt->line, stmt->column, "cannot redefine default of const field '" + typeNameForError + "." + fa->field + "'");
                    return;
                }

                auto valueType = analyzeExpr(assign->value, field->type);

                if (valueType && !isImplicitlyConvertible(valueType, field->type)) {
                    error(stmt->line, stmt->column, "cannot redefine default of '" + typeNameForError + "." + fa->field + "': expected '" + typeToString(field->type) + "', got '" + typeToString(valueType) + "'");
                    return;
                }

                fa->resolvedField = field;
                fa->isTypeDefaultFieldAccess = true;
                fa->resolvedType = field->type;
                return;
            }
        }

        auto targetType = analyzeExpr(assign->target);
        auto valueType = analyzeExpr(assign->value, targetType);    //  Тип target — ожидаемый для контекстной типизации литералов в правой части

        //  Цель присваивания должна быть lvalue: foo() = 5, 1 = x, (a+b) = 0 — ошибка.
        if (!isLvalue(assign->target)) {
            error(stmt->line, stmt->column, "left-hand side of assignment is not an lvalue");
        }

        //  Проверка const на корневой символ цели: покрывает Identifier,
        //  FieldAccess (obj.field, obj.a.b), ArrayAccess (arr[i], m[i][j]) и NamespaceAccess
        auto rootSym = resolveTargetRoot(assign->target);
        if (rootSym && rootSym->isConst) {
            error(stmt->line, stmt->column, "cannot assign to const '" + rootSym->name + "'");
        }
        // Проверка const для неявного self-поля:
        // class A { const int x = 1; void f() { x = 2; } }
        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            if (id->resolvedField && id->resolvedField->isConst) {
                error(stmt->line, stmt->column, "cannot assign to const field '" + id->name + "'");
            }
        }

        //  Проверка const самого поля при obj.x = ... (включая цепочки obj.a.b = ...)
        if (auto* fa = dynamic_cast<FieldAccess*>(assign->target)) {
            auto objType = fa->object->resolvedType;
            if (objType && (objType->kind == TypeKind::Struct || objType->kind == TypeKind::Class)) {
                auto typeSym = resolveQualifiedSymbol(objType->name);
                const std::vector<FieldInfo>* fields = nullptr;
                if (typeSym) {
                    if (typeSym->structInfo) fields = &typeSym->structInfo->fields;
                    else if (typeSym->classInfo) fields = &typeSym->classInfo->fields;
                }
                if (fields) {
                    for (auto& f : *fields) {
                        if (f.name == fa->field && f.isConst) {
                            error(stmt->line, stmt->column, "cannot assign to const field '" + fa->field + "'");
                            break;
                        }
                    }
                }
            }
        }

        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            auto sym = table.resolve(id->name);
            if (sym) {
                sym->isInitialized = true; //  Помечаем переменную как инициализированную
            }
        }

        if (targetType && valueType) {
            if (assign->op == AssignOp::Assign) {
                //  Обычное присваивание: правая часть должна неявно приводиться к типу цели
                if (!isImplicitlyConvertible(valueType, targetType)) {
                    error(stmt->line, stmt->column, "type mismatch in assignment: cannot assign '" + typeToString(valueType) + "' to '" + typeToString(targetType) + "'");
                }
            }
            else {
                //  Compound: target = target OP value. Проверяем, что операция допустима
                //  для (targetType, valueType), и что её результат укладывается обратно в target.
                bool opOk = false;
                std::shared_ptr<Type> resultType;

                if (assign->op == AssignOp::AddAssign && targetType->kind == TypeKind::String) {
                    //  Конкатенация строки: string += string | char
                    if (valueType->kind == TypeKind::String || valueType->kind == TypeKind::Char) {
                        opOk = true;
                        resultType = targetType;
                    }
                }
                if (assign->op == AssignOp::ModAssign) {
                    if (!isIntType(targetType) || !isIntType(valueType)) {
                        error(assign->line, assign->column, "operator '%=' requires integer operands");
                        return;
                    }
                    opOk = true;
                    resultType = targetType;
                }
                else if (isNumericType(targetType) && isNumericType(valueType)) {
                    //  Числовые compound (+=, -=, *=, /=, %=): результат — общий числовой тип
                    resultType = commonType(targetType, valueType);
                    opOk = (resultType != nullptr);
                }

                if (!opOk) {
                    error(stmt->line, stmt->column, "compound operator: incompatible operand types '" + typeToString(targetType) + "' and '" + typeToString(valueType) + "'");
                }
                else if (resultType && !isImplicitlyConvertible(resultType, targetType)) {
                    //  Запрещаем сужение: int8 += int64 даёт int64, который не лезет обратно в int8
                    error(stmt->line, stmt->column, "compound operator: result type '" + typeToString(resultType) + "' cannot be assigned to '" + typeToString(targetType) + "' (narrowing)");
                }
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<If*>(stmt)) {  //  if
        auto condType = analyzeExpr(ifStmt->condition);
        if (condType && condType->kind != TypeKind::Bool) {   //  Условие всегда типа bool
            error(stmt->line, stmt->column, "if condition must be bool, got '" + typeToString(condType) + "'");
        }
        analyzeStmt(ifStmt->thenBranch);
        if (ifStmt->elseBranch) {
            analyzeStmt(ifStmt->elseBranch);
        }
    }
    else if (auto* whileStmt = dynamic_cast<While*>(stmt)) {  //  while 
        auto condType = analyzeExpr(whileStmt->condition);
        if (condType && condType->kind != TypeKind::Bool) { // условие также всегда должно быть bool
            error(stmt->line, stmt->column, "while condition must be bool, got '" + typeToString(condType) + "'");
        }
        loopDepth++;    //  Вошли в цикл на +1 глубину вложенности
        analyzeStmt(whileStmt->body);   //  Осмотрели чё там делается
        loopDepth--;    //  Вышли обратно
    }
    else if (dynamic_cast<Break*>(stmt)) {  //  Break
        if (loopDepth == 0) {   //  Break нельзя ставить на нулевой глубине, то бишь вне цикла
            error(stmt->line, stmt->column, "'break' outside of loop");   
        }
    }
    else if (dynamic_cast<Continue*>(stmt)) {
        if (loopDepth == 0) {   //  Continue тоже
            error(stmt->line, stmt->column, "'continue' outside of loop");
        }
    }
    else if (auto* ret = dynamic_cast<Return*>(stmt)) {  // Return
        if (ret->value) {
            //  return value; в void-функции — отдельное сообщение об ошибке
            if (currentReturnType && currentReturnType->kind == TypeKind::Void) {
                analyzeExpr(ret->value);  //  всё равно проанализировать, чтобы собрать ошибки внутри
                error(stmt->line, stmt->column, "void function cannot return a value");
            }
            else {
                auto valType = analyzeExpr(ret->value, currentReturnType);
                if (valType && currentReturnType) {
                    if (!isImplicitlyConvertible(valType, currentReturnType)) {
                        error(stmt->line, stmt->column, "return type mismatch: expected '" + typeToString(currentReturnType) + "', got '" + typeToString(valType) + "'");
                    }
                }
            }
        }
        else {  //  return; без значения — функция должна быть void
            if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
                error(stmt->line, stmt->column, "return without value in non-void function");
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {  //  выражение как инструкция (напр. print(x);)
        analyzeExpr(exprStmt->expr);
    }
    else if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {  //  Объявление функции
        auto sym = table.resolve(func->name);   //  Имя уже зарегистрировано в collectTopLevel — здесь только анализ тела
        auto prevReturnType = currentReturnType;    //  Сохраняем предыдущий тип возврата и ставим текущий (для проверки return)
        if (sym && sym->funcInfo) {
            currentReturnType = sym->funcInfo->returnType;
        }
        else {
            currentReturnType = nullptr;
        }
        table.enterScope();     //  Тело функции — новый scope, в который добавляем параметры с типами
        if (sym && sym->funcInfo) {
            for (size_t j = 0; j < func->params.size(); j++) {  //  Регистрируем параметры
                auto paramSym = std::make_shared<Symbol>();
                paramSym->name = func->params[j].name;
                paramSym->kind = SymbolKind::Variable;
                paramSym->isInitialized = true;  //  параметры всегда инициализированы
                paramSym->isConst = func->params[j].isConst;  //  const на параметре
                if (j < sym->funcInfo->params.size()) {
                    paramSym->type = sym->funcInfo->params[j].type;
                }
                else {
                    paramSym->type = nullptr;
                }

                auto paramResult = table.declare(paramSym);
                if (!paramResult) {
                    error(stmt->line, stmt->column, paramResult.error());
                }
                else {
                    func->params[j].resolvedSym = paramSym;
                }
            }
        }
        //  Анализируем тело без лишнего enterScope — Block сам его создаст
        for (auto* stmt : func->body->statements) {
            analyzeStmt(stmt);
        }
        table.exitScope();

        //  Проверяем что non-void функция возвращает значение на всех путях
        if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
            if (!alwaysReturns(func->body)) {
                error(stmt->line, stmt->column, "function '" + func->name + "' does not return a value on all paths");
            }
        }

        //  Восстанавливаем тип возврата внешней функции (для вложенных объявлений)
        currentReturnType = prevReturnType;
    }
    else if (dynamic_cast<StructDecl*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
    }
    else if (dynamic_cast<TypeAlias*>(stmt)) {
        //  Уже зарегистрирован в collectTopLevel — пропускаем
    }
    else if (auto* clas = dynamic_cast<ClassDecl*>(stmt)) {  //  Объявление класса
        auto classSym = table.resolve(clas->name);
        if (!classSym || !classSym->classInfo) return;

        auto selfieldType = makeType(TypeKind::Class);  //  Тип self для методов
        selfieldType->name = classSym->type ? classSym->type->name : clas->name;

        //  Внутри тела поля класса не объявляются как локальные имена —
        //  они разрешаются через currentClass как неявное self.<field>.
        auto prevClass = currentClass;
        currentClass = classSym->classInfo;

        for (auto* method : clas->methods) {    //  Анализируем тела методов
            auto prevRetType = currentReturnType;
            auto currMethod = classSym->classInfo->methods.find(method->name);
            if (currMethod != classSym->classInfo->methods.end())
                currentReturnType = currMethod->second->returnType;
            else
                currentReturnType = nullptr;

            table.enterScope();
            auto selfSym = std::make_shared<Symbol>();  //  self — единственное явно объявленное имя
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfieldType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            if (currMethod != classSym->classInfo->methods.end()) {
                for (size_t j = 0; j < method->params.size(); j++) {
                    auto paramSym = std::make_shared<Symbol>();
                    paramSym->name = method->params[j].name;
                    paramSym->kind = SymbolKind::Variable;
                    paramSym->isInitialized = true;
                    paramSym->isConst = method->params[j].isConst;
                    if (j < currMethod->second->params.size())
                        paramSym->type = currMethod->second->params[j].type;
                    else
                        paramSym->type = nullptr;
                    auto result = table.declare(paramSym);
                    if (!result) {
                        error(method->line, method->column, result.error());
                    }
                    else {
                        method->params[j].resolvedSym = paramSym;
                    }
                }
            }

            for (auto* stmt : method->body->statements) {
                analyzeStmt(stmt);
            }
            table.exitScope();

            if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
                if (!alwaysReturns(method->body)) {
                    error(stmt->line, stmt->column, "method '" + clas->name + "." + method->name + "' does not return a value on all paths");
                }
            }
            currentReturnType = prevRetType;
        }

        //  Анализируем тело конструктора
        if (clas->constructor) {
            auto prevRetType = currentReturnType;
            currentReturnType = makeType(TypeKind::Void);

            table.enterScope();
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfieldType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            if (classSym->classInfo->constructor) {
                for (size_t j = 0; j < clas->constructor->params.size(); j++) {
                    auto paramSym = std::make_shared<Symbol>();
                    paramSym->name = clas->constructor->params[j].name;
                    paramSym->kind = SymbolKind::Variable;
                    paramSym->isInitialized = true;
                    paramSym->isConst = clas->constructor->params[j].isConst;
                    if (j < classSym->classInfo->constructor->params.size()) {
                        paramSym->type = classSym->classInfo->constructor->params[j].type;
                    }
                    else {
                        paramSym->type = nullptr;
                    }
                    auto result = table.declare(paramSym);
                    if (!result) {
                        error(clas->constructor->line, clas->constructor->column, result.error());
                    }
                    else {
                        clas->constructor->params[j].resolvedSym = paramSym;
                    }
                }
            }

            for (auto* stmt : clas->constructor->body->statements) {
                analyzeStmt(stmt);
            }
            table.exitScope();
            currentReturnType = prevRetType;
        }

        //  Анализируем тело деструктора
        if (clas->destructor) {
            auto prevRetType = currentReturnType;
            currentReturnType = makeType(TypeKind::Void);

            table.enterScope();
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfieldType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            for (auto* stmt : clas->destructor->body->statements) {
                analyzeStmt(stmt);
            }
            table.exitScope();
            currentReturnType = prevRetType;
        }

        currentClass = prevClass;
    }
    else if (auto* namespase = dynamic_cast<NamespaceDecl*>(stmt)) {    //  Входим в сохранённый scope и анализируем тела объявлений
        auto namespaceSym = table.resolve(namespase->name);
        if (namespaceSym && namespaceSym->namespaceScope) {
            table.pushScope(namespaceSym->namespaceScope);
            std::string prevNamespace = currentNamespace;
            currentNamespace = appendQualifiedName(currentNamespace, namespase->name);
            for (auto* decl : namespase->decls) {
                analyzeStmt(decl);
            }
            currentNamespace = prevNamespace;
            table.exitScope();
        }
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(stmt)) {  //  export — анализируем обёрнутое объявление,
        analyzeStmt(exp->decl);                              //  затем помечаем созданные символы как exported.
        //  Глобальные переменные регистрируются только в этом проходе (в collectTopLevel
        //  для VarDecl ветки нет), поэтому isExported нужно проставлять здесь.
        if (auto* var = dynamic_cast<VarDecl*>(exp->decl)) {
            for (auto* v : var->vars) {
                auto sym = table.resolve(v->name);
                if (sym) sym->isExported = true;
            }
        }
    }
}

//  Преобразование типа из libclang в поддерживаемый нами тип
//  Возвращает nullptr, если тип не поддерживается
static std::shared_ptr<Type> mapCType(CXType cType) {
    CXType canon = clang_getCanonicalType(cType);   //  Берём самый базовый тип в обход синонимов и прочих обёрток

    if (canon.kind == CXType_Pointer) {     //  Указатели обрабатываем отдельно
        CXType pointer = clang_getCanonicalType(clang_getPointeeType(canon));   //  Базовый тип на который ссылается указатель
        
        if (pointer.kind == CXType_Char_S || pointer.kind == CXType_SChar || pointer.kind == CXType_Char_U || pointer.kind == CXType_UChar) {
            return makeType(TypeKind::String);  //  signed и unsigned char указатели -> это строка
        }
        return makeType(TypeKind::Uint64);  //  Любой другой указатель -> сырой адрес
    }

    //  Примитивные типы
    switch (canon.kind) {
        case CXType_Void:       return makeType(TypeKind::Void);
        case CXType_Bool:       return makeType(TypeKind::Bool);

        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Char_U:
        case CXType_UChar:      return makeType(TypeKind::Int8);

        case CXType_Short:      return makeType(TypeKind::Int16);
        case CXType_UShort:     return makeType(TypeKind::Uint16);

        case CXType_Int:        return makeType(TypeKind::Int32);
        case CXType_UInt:       return makeType(TypeKind::Uint32);

        case CXType_Long:
        case CXType_LongLong:   return makeType(TypeKind::Int64);

        case CXType_ULong:
        case CXType_ULongLong:  return makeType(TypeKind::Uint64);

        case CXType_Float:      return makeType(TypeKind::Float32);
        case CXType_Double:     return makeType(TypeKind::Float64);

        default:                return nullptr;
    }
}

//  Проверяет, принадлежит ли курсор нужному заголовку (по basename файла)
static bool isFromHeader(CXCursor cursor, const std::string& wantedBase) {
    CXSourceLocation loc = clang_getCursorLocation(cursor); //  Ищем позицию курсора в файле  
    CXFile file;
    unsigned line, col, off;
    clang_getFileLocation(loc, &file, &line, &col, &off);   //  Ищем расположение файла с курсором
    if (!file) return false;

    CXString fname = clang_getFileName(file);   //  Берём имя из расположения  
    std::string base = std::filesystem::path(clang_getCString(fname)).filename().string();  //  Сохраняем имя
    clang_disposeString(fname); //  Очищаем буфер
    return base == wantedBase;  //  Смотрим является ли файл с функцией тем же откуда мы пытаемся его взять
}

//  Извлекает имя курсора в std::string
static std::string getCursorName(CXCursor cursor) {
    CXString name = clang_getCursorSpelling(cursor);
    std::string result = clang_getCString(name);
    clang_disposeString(name);
    return result;
}

//  Пытается создать символ MyLang из C-декларации функции.
//  Возвращает nullptr, если типы параметров/возврата не поддерживаются.
static std::shared_ptr<Symbol> makeCFuncSymbol(CXCursor cursor) {
    //  Маппим возвращаемый тип
    auto retType = mapCType(clang_getCursorResultType(cursor));
    if (!retType) return nullptr;

    //  Маппим параметры
    int nargs = clang_Cursor_getNumArguments(cursor);
    auto info = std::make_shared<FuncInfo>();
    info->returnType = retType;
    info->isExternC = true;
    info->isVariadic = clang_Cursor_isVariadic(cursor);

    for (int i = 0; i < nargs; i++) {
        CXCursor arg = clang_Cursor_getArgument(cursor, i);
        auto paramType = mapCType(clang_getCursorType(arg));
        if (!paramType) return nullptr;
        info->params.push_back({getCursorName(arg), paramType});
    }

    //  Создаём символ
    auto sym = std::make_shared<Symbol>();
    sym->name = getCursorName(cursor);
    sym->kind = SymbolKind::Function;
    sym->type = retType;
    sym->funcInfo = info;
    sym->isInitialized = true;
    return sym;
}

void SemanticAnalyzer::processCImport(ImportDecl* imp) {
    std::string stub = "#include <" + imp->path + ">\n";    //  Создаём виртуальный файл с единственной строкой #include <header>
    const char* virtualName = "cImport.c";     //  Читать будем с этого виртуального файла чтобы не создавать временный

    CXUnsavedFile unsaved;  //  unsaved позволяет читать не с диска, а с файла
    unsaved.Filename = virtualName;
    unsaved.Contents = stub.c_str();    //  Берём указатель на данные библиотеки
    unsaved.Length = stub.size();

    CXIndex index = clang_createIndex(0, 0);    //  Создание текущей сессии для Си компилятора, без этого не будет работать
    CXTranslationUnit res = nullptr;    //  Сюда положим результат парсинга

    std::string includeDir = "-I" + std::filesystem::path(currentFilePath).parent_path().string();
    const char* args[] = {"-x", "c", includeDir.c_str()};   //  -x, c - означает что язык всегда Си вне зависимости от разрешения файла
    
    //  Парсим функции без тел. 
    CXErrorCode err = clang_parseTranslationUnit2(index, virtualName, args, 3, &unsaved, 1, CXTranslationUnit_SkipFunctionBodies, &res);

    if (err != CXError_Success || !res) {   
        error(imp->line, imp->column, "libclang failed to parse header '" + imp->path + "'");
        clang_disposeIndex(index);
        return;
    }

    std::string wantedBase = std::filesystem::path(imp->path).filename().string();  //  Имя библиотеки напрямую без пути
    CImportVisitorCtx ctx{wantedBase, &table, 0, 0}; // Необходимые данные для обхода AST Си библиотеки (0, 0 - счётчики зарегестрированных и пропущенных функций)  

    CXCursor root = clang_getTranslationUnitCursor(res);    //  Корневой узел AST библиотеки
    clang_visitChildren(root, [](CXCursor cursor, CXCursor, CXClientData data) -> CXChildVisitResult {  //  Обходим дерево
            auto* ctx = (CImportVisitorCtx*)data;       //  Каждый Cursor - какой либо элемент из Си

            if (clang_getCursorKind(cursor) != CXCursor_FunctionDecl)   //  Нам нужны только функции
                return CXChildVisit_Continue;   //  Всё остальное скипаем

            if (!isFromHeader(cursor, ctx->wantedBase)) //  Подключаем функции только из заявленного headera не из зависимых
                return CXChildVisit_Continue;

            auto sym = makeCFuncSymbol(cursor); //  Создаём символ из функции
            if (!sym) {
                ctx->skipped++;
                return CXChildVisit_Continue;
            }

            if (ctx->table->declare(sym))   //  Записываем символ в таблицу
                ctx->registered++;
            else
                ctx->skipped++;     //  Ну или скипаем если не подходит

            return CXChildVisit_Continue;
        }, &ctx);

    clang_disposeTranslationUnit(res);  //  Закрываем сессию парсинга 
    clang_disposeIndex(index);
}

void SemanticAnalyzer::importExportedSymbolsFrom(SemanticAnalyzer& module) {
    auto importedScope = module.table.currentScope();

    for (auto& [name, sym] : importedScope->symbols) {
        if (!sym) continue;

        bool isBuiltin = name == "print" || name == "len" || name == "input" || name == "exit" || name == "panic" || name == "push" || name == "pop";

        //  Пропускаем встроенные функции
        if (isBuiltin) {
            continue;
        }

        //  Пропускаем не помеченные exportom функции 
        if (!sym->isExported) {
            continue;
        }

        //  Остальное добавляем в основную таблицу символов
        table.declare(sym);
    }
}

void SemanticAnalyzer::processImport(ImportDecl* imp, Program* ownerProgram) {
    if (imp->isC) {     //  Если Сишная функция
        processCImport(imp);
        return;
    }

    std::filesystem::path base = std::filesystem::path(currentFilePath).parent_path();  //  Берём путь текущего файла
    std::filesystem::path target = base / imp->path;    //  Разрешаем путь относительно текущего файла

    if (!std::filesystem::exists(target)) {
        error(imp->line, imp->column, "cannot open imported file '" + imp->path + "'");
        return;
    }

    std::string absPath = std::filesystem::canonical(target).string();

    if (importedFiles.contains(absPath)) return;    //  Защита от циклических импортов
    importedFiles.insert(absPath);  //  Добавляем файл в множество импортов  

    std::ifstream file(absPath);    //  Читаем файл
    if (!file.is_open()) {
        error(imp->line, imp->column, "cannot open imported file '" + imp->path + "'");
        return;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();

    auto tokens = tokenize(source, absPath);     //  Токенизируем
    if (!tokens) {
        error(imp->line, imp->column, "in '" + imp->path + "': " + tokens.error());
        return;
    }

    auto nodes = parse(*tokens, absPath);    //  Парсим
    if (!nodes) {
        error(imp->line, imp->column, "in '" + imp->path + "': " + nodes.error());
        return;
    }

    Program importedProgram;
    importedProgram.decls = *nodes;

    SemanticAnalyzer moduleAnalyzer;    //  У каждого нового модуля свой анализатор со своей таблицей символов и внутренними состояниями

    // Передаём список уже импортированных файлов,
    // чтобы A -> B -> A не уходило в рекурсию.
    moduleAnalyzer.importedFiles = importedFiles;

    auto result = moduleAnalyzer.analyzeModule(&importedProgram, absPath, false);   // Анализируем

    importedFiles.insert(
        moduleAnalyzer.importedFiles.begin(),   //  Собираем все модули которые были рекурсивно разобраны в изначальный файл    
        moduleAnalyzer.importedFiles.end()
    );

    if (!result) {
        errors.push_back(result.error());
        return;
    }

    importExportedSymbolsFrom(moduleAnalyzer);  //  Собираем только то что разрешено через Export

    if (ownerProgram) {
        ownerProgram->imports.insert(   //  Добавлям AST импортируемого модуля к нашему основному AST
            ownerProgram->imports.end(),
            importedProgram.imports.begin(),
            importedProgram.imports.end()
        );

        ownerProgram->imports.insert(
            ownerProgram->imports.end(),
            importedProgram.decls.begin(),
            importedProgram.decls.end()
        );
    }
}

std::expected<void, std::string> SemanticAnalyzer::analyzeModule(Program* program, const std::string& filePath, bool requireMain) {
    errors.clear(); //  Очистка ошибок
    invalidTopLevelDecls.clear();   //  Собранные невалидные модули убираем

    currentFilePath = std::filesystem::canonical(filePath).string();    //  Указываем текущий путь файла
    importedFiles.insert(currentFilePath);  //  Уже импортируеммые файлы сохраняем
    currentNamespace.clear();   //  Текущее пространство имён очищаем

    registerBuiltins(); //  Регистрируем в глобальном scope встроенные функции

    bool seenNonImport = false;

    for (auto* decl : program->decls) { //  Первый обход ищет все import по порядку сверху вниз
        if (auto* imp = dynamic_cast<ImportDecl*>(decl)) {
            if (seenNonImport) {   
                error(decl->line, decl->column, "'import' must appear before any other top-level declaration");
            }

            processImport(imp, program);    //  Проверяем import
        }
        else {
            seenNonImport = true;   //  Когда встретили не Import ставится флаг и все последующие import будут вызывать ошибку выше
        }
    }

    predeclareTopLevel(program->decls); //  Второй проход, регистрируем в таблицу заглушки имен без сигнатур
    collectTopLevel(program->decls);    //  Третий проход заместо заглушек полноценно ставим заполненные сущности 

    for (auto* decl : program->decls) {
        if (dynamic_cast<ImportDecl*>(decl)) {
            continue;
        }

        analyzeStmt(decl);
    }

    if (requireMain) {
        auto mainSym = table.resolve("main");

        if (!mainSym || mainSym->kind != SymbolKind::Function) {
            error(0, 0, "missing entry point: expected function 'int main()'");
        }
        else if (!mainSym->funcInfo) {
            error(0, 0, "'main' is not a function");
        }
        else {
            if (!mainSym->funcInfo->returnType ||
                mainSym->funcInfo->returnType->kind != TypeKind::Int32) {
                error(0, 0, "'main' must return 'int', got '" + typeToString(mainSym->funcInfo->returnType) + "'");
            }

            if (!mainSym->funcInfo->params.empty()) {
                error(0, 0, "'main' must take no parameters");
            }
        }
    }

    if (!errors.empty()) {
        std::string message;

        for (auto& err : errors) {
            message += err + "\n";
        }

        return std::unexpected(message);
    }

    return {};
}

std::expected<void, std::string> SemanticAnalyzer::analyze(Program* program, const std::string& filePath) {
    return analyzeModule(program, filePath, true);
}
