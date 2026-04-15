#include "SymbolTable.hpp"
#include "Ast.hpp"
#include <expected>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <clang-c/Index.h>

// функции для работы с типами

static std::shared_ptr<Type> makeType(TypeKind kind) {  //  Определение сущности типа
    auto t = std::make_shared<Type>();  
    t->kind = kind;
    return t;
}

static std::shared_ptr<Type> makeArrayType(std::shared_ptr<Type> elem, int size) {  // Тоже самое для массивов
    auto t = std::make_shared<Type>();
    t->kind = TypeKind::Array;
    t->elementType = elem;  //  Тип массива определяем по первому элементу
    t->arraySize = size;
    return t;
}

static bool typesEqual(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) {     //  Кого-то из типов несуществует
        return false;
    }     
    if (a->kind != b->kind) {   //  Если типы объекты разные по сущности
        return false;
    }
    if (a->kind == TypeKind::Array) {   //  Массивы рекурсивно проверяем по типам первых элементов
        return typesEqual(a->elementType, b->elementType) && a->arraySize == b->arraySize;  // Разные размеры == разные типы
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
    if (!type) "<unknown>";                                            //  Это для записи в ошибки  
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
        case TypeKind::Array:   return typeToString(type->elementType) + "[" + std::to_string(type->arraySize) + "]";
        case TypeKind::DynArray: return typeToString(type->elementType) + "[]";
        case TypeKind::Struct:  return type->name;
        case TypeKind::Class:   return type->name;
        case TypeKind::Alias:   return type->name;
        default:                return "<unknown>";
    }
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

    //  Любой int/uint → float64
    if (isIntType(from) && to->kind == TypeKind::Float64)
        return true;

    //  Array -> DynArray 
    //  Позволяет: int[] arr = [1, 2, 3];  и  int[][] m = [[1, 2], [3, 4]];
    //  Рекурсивно проверяем элементы (для вложенных массивов)
    if (from->kind == TypeKind::Array && to->kind == TypeKind::DynArray)
        return from->elementType && to->elementType && isImplicitlyConvertible(from->elementType, to->elementType);

    return false;
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

    return false;
}

//  Определяет общий тип для бинарной операции (к чему привести оба операнда)
//  Например: int32 + int64 -> int64, int + float -> float64
//  Возвращает nullptr если типы несовместимы
static std::shared_ptr<Type> commonType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return nullptr;

    if (typesEqual(a, b)) return a;  //  одинаковые — ничего приводить не надо

    //  Оба signed int — берём больший
    if (a->kind >= TypeKind::Int8 && a->kind <= TypeKind::Int64 && b->kind >= TypeKind::Int8 && b->kind <= TypeKind::Int64) {
        return typeRank(a) >= typeRank(b) ? a : b;
    }

    //  Оба unsigned int — берём больший
    if (a->kind >= TypeKind::Uint8 && a->kind <= TypeKind::Uint64 && b->kind >= TypeKind::Uint8 && b->kind <= TypeKind::Uint64) {
        return typeRank(a) >= typeRank(b) ? a : b;
    }

    //  Оба float — берём больший
    if (isFloatType(a) && isFloatType(b)) {
        return typeRank(a) >= typeRank(b) ? a : b;
    }

    //  int/uint + float → float64
    if (isIntType(a) && isFloatType(b)) {
        return makeType(TypeKind::Float64);
    }

    if (isFloatType(a) && isIntType(b)) {
        return makeType(TypeKind::Float64);
    }

    return nullptr;  //  несовместимы 
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

std::shared_ptr<Type> SemanticAnalyzer::resolveTypeName(const std::string& name) {
    //  Переводим полученные из AST типы в виде строк в объекты вида Type
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
    if (name.size() > 2 && name.back() == ']') {    //  Массивный тип данных T[] - минимум 3 символа
        auto openBracket = name.rfind('[');     //  Ищем последнюю открывающую '[' для случаев int[][] нужно сперва искать именно последнюю 
        if (openBracket == std::string::npos) { //  Не нашли скобку
            return nullptr;
        }

        std::string baseName = name.substr(0, openBracket); //  Базовый тип — всё до последнего '['
        std::string inside = name.substr(openBracket + 1, name.size() - openBracket - 2);   //  Содержимое между [ и ] — пустое для [], число для [N]
        auto elemType = resolveTypeName(baseName);  //  Рекурсивно разрешаем базовый тип (для int[][] сначала разрешит int[])
        
        if (!elemType) return nullptr;

        if (inside.empty()) {   //  T[] — динамический массив
            auto type = std::make_shared<Type>();
            type->kind = TypeKind::DynArray;
            type->elementType = elemType;
            return type;
        } 
        else {  //  T[N] — фиксированный массив
            return makeArrayType(elemType, std::stoi(inside));
        }
    }

    //  Пользовательский тип — ищем в таблице символов
    auto sym = table.resolve(name);
    if (sym) {
        if (sym->kind == SymbolKind::Struct) {
            auto type = makeType(TypeKind::Struct);
            type->name = name;
            return type;
        }
        if (sym->kind == SymbolKind::Class) {
            auto type = makeType(TypeKind::Class);
            type->name = name;
            return type;
        }
        if (sym->kind == SymbolKind::TypeAlias) {
            return sym->type;  //  alias разрешается в оригинальный тип
        }
    }

    return nullptr;  //  Тип не найден
}

// Обход AST

std::shared_ptr<Type> SemanticAnalyzer::analyzeExpr(Expr* expr, std::shared_ptr<Type> expected) {
    if (!expr) return nullptr;  //  В случае int x; без инициализатора — ничего не делаем

    if (auto* num = dynamic_cast<Number*>(expr)) {  //  Числовой литерал
        //  Контекстная типизация: если есть ожидаемый тип и он совместим по категории (int/float), берём его
        if (num->isFloat) {
            if (expected && isFloatType(expected))
                expr->resolvedType = expected;
            else
                expr->resolvedType = makeType(TypeKind::Float64);   //  3.14 -> float64 по умолчанию
        } else {
            if (expected && (isIntType(expected) || isFloatType(expected)))
                expr->resolvedType = expected;  
            else
                expr->resolvedType = makeType(TypeKind::Int32);     //  42 -> int32 по умолчанию
        }
        return expr->resolvedType;
    }

    //  Строковый литерал -> string
    if (dynamic_cast<String*>(expr)) {
        expr->resolvedType = makeType(TypeKind::String);
        return expr->resolvedType;
    }

    //  Булев литерал -> bool
    if (dynamic_cast<Bool*>(expr)) {
        expr->resolvedType = makeType(TypeKind::Bool);
        return expr->resolvedType;
    }

    //  Идентификатор
    if (auto* id = dynamic_cast<Identifier*>(expr)) {   //  Ищем имя в таблице символов, проверяем что переменная инициализирована
        auto sym = table.resolve(id->name);
        if (!sym) {
            error(expr->line, "'" + id->name + "' is not declared");
            return nullptr; //  Нет такого идентификатора
        }
        if (!sym->isInitialized && sym->kind == SymbolKind::Variable) {
            error(expr->line, "'" + id->name + "' is used before initialization");
        }   //  Переменная объявлена но не инициализирована 
        expr->resolvedType = sym->type;
        return expr->resolvedType;
    }

    //  Бинарная операция
    if (auto* bin = dynamic_cast<Binary*>(expr)) {  //  Рекурсивно анализируем оба операнда, потом проверяем совместимость типов
        auto leftType = analyzeExpr(bin->left);
        auto rightType = analyzeExpr(bin->right);

        if (!leftType || !rightType) return nullptr;    //  Если хотя бы один операнд не определён — не можем проверить

        switch (bin->op) {  //  Операнд
            case Operand::Add:
                if (leftType->kind == TypeKind::String && rightType->kind == TypeKind::String) {
                    expr->resolvedType = makeType(TypeKind::String);  //  "Hello" + "World" -> string
                    return expr->resolvedType;
                }
                [[fallthrough]];  //  иначе — обычная арифметика

            case Operand::Sub:    
            case Operand::Mul:
            case Operand::Div:
            case Operand::Mod:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, "arithmetic operator requires numeric operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                {
                    auto common = commonType(leftType, rightType);  //  Неявное приведение: int32 + int64 -> int64, int + float -> float64
                    if (!common) {
                        error(expr->line, "incompatible types in arithmetic: '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                        return nullptr;
                    }
                    expr->resolvedType = common;  
                }
                return expr->resolvedType;

            case Operand::Less:     //  Сравнения
            case Operand::Greater:
            case Operand::LessEqual:
            case Operand::GreaterEqual:
                if (!isNumericType(leftType) || !isNumericType(rightType)) {
                    error(expr->line, "comparison requires numeric operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                if (!commonType(leftType, rightType)) {
                    error(expr->line, "incompatible types in comparison: '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);  //  Возвращаемый тип вседа bool
                return expr->resolvedType;

            case Operand::EqualEqual:   //  Равенства
            case Operand::NotEqual:
                if (!typesEqual(leftType, rightType)) {
                    error(expr->line, "cannot compare '" + typeToString(leftType) + "' with '" + typeToString(rightType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            case Operand::And:      //  И, ИЛИ
            case Operand::Or:
                if (leftType->kind != TypeKind::Bool || rightType->kind != TypeKind::Bool) {
                    error(expr->line, "logical operator requires bool operands, got '" + typeToString(leftType) + "' and '" + typeToString(rightType) + "'");
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
        auto operandType = analyzeExpr(unary->operand); //  Чё мы пытаемся унарнуть
        if (!operandType) return nullptr;

        switch (unary->op) {
            case Operand::UnaryMinus:
            case Operand::UnaryPlus:
                if (!isNumericType(operandType)) {  //  Обязано быть число для + и -
                    error(expr->line, "unary +/- requires numeric operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = operandType;
                return expr->resolvedType;

            case Operand::Not:
                if (operandType->kind != TypeKind::Bool) {  //  Для отрицания всегда bool
                    error(expr->line, "'!' requires bool operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
                }
                expr->resolvedType = makeType(TypeKind::Bool);
                return expr->resolvedType;

            case Operand::Increment:
            case Operand::Decrement:
                if (!isIntType(operandType)) {  //  Только целые числа
                    error(expr->line, "++/-- requires integer operand, got '" + typeToString(operandType) + "'");
                    return nullptr;
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
                    error(expr->line, "method call on non-class type '" + typeToString(objType) + "'");
                return nullptr;
            }

            auto classSym = table.resolve(objType->name);   //  Ищем класс в таблице символов
            if (!classSym || !classSym->classInfo) {
                return nullptr;
            }

            const std::string& methodName = fieldCallee->field; 
            auto method = classSym->classInfo->methods.find(methodName);    //  Ищем метод в классе
            if (method == classSym->classInfo->methods.end()) {
                error(expr->line, "class '" + objType->name + "' has no method '" + methodName + "'");
                for (auto* arg : call->args) {
                    analyzeExpr(arg);   //  Также снова проходимся по параметрам для сбора ошибок
                }
                return nullptr;
            }

            auto& methodInfo = method->second;  //  Достаём параметры метода
            std::vector<std::shared_ptr<Type>> argTypes;
            for (size_t i = 0; i < call->args.size(); i++) {
                std::shared_ptr<Type> expectedArg = nullptr;
                if (i < methodInfo->params.size())
                    expectedArg = methodInfo->params[i].second; //  Для каждого параметра берём ожидаемый тип 
                argTypes.push_back(analyzeExpr(call->args[i], expectedArg)); // Пробрасываем его для проверки
            }

            if (argTypes.size() != methodInfo->params.size()) {
                error(expr->line, "method '" + fieldCallee->field + "' expects " + std::to_string(methodInfo->params.size()) + " arguments, got " + std::to_string(argTypes.size()));
            } 
            else {
                for (size_t j = 0; j < argTypes.size(); j++) {
                    if (argTypes[j] && methodInfo->params[j].second && !isImplicitlyConvertible(argTypes[j], methodInfo->params[j].second)) {
                        error(expr->line, "method '" + fieldCallee->field + "' argument " + std::to_string(j + 1) + ": cannot convert '" + typeToString(argTypes[j]) + "' to '" + typeToString(methodInfo->params[j].second) + "'");
                    }   //  Если переданные параметры не конвертируются в заданные
                }
            }

            expr->resolvedType = methodInfo->returnType;
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
        for (size_t i = 0; i < call->args.size(); i++) {
            std::shared_ptr<Type> expectedArg = nullptr;
            if (sym->funcInfo && callee->name != "print" && callee->name != "len" && i < sym->funcInfo->params.size()) {
                expectedArg = sym->funcInfo->params[i].second;  //  print и len особые случаи которые не выражаются общей семантикой
            }
            argTypes.push_back(analyzeExpr(call->args[i], expectedArg));
        }

        //  Проверяем типы аргументов
        if (sym->funcInfo && callee->name != "print" && callee->name != "len") {
            bool variadic = sym->funcInfo->isVariadic;
            size_t fixed = sym->funcInfo->params.size();
            bool arityOk = variadic ? (argTypes.size() >= fixed) : (argTypes.size() == fixed);  //  Может ли функция принимать сколько угодно параметров
            if (!arityOk) {
                error(expr->line, "'" + callee->name + "' expects " + std::string(variadic ? "at least " : "") + std::to_string(fixed) + " arguments, got " + std::to_string(argTypes.size()));
            }
            else {  //  Типы фиксированных параметров
                for (size_t j = 0; j < fixed; j++) {
                    if (argTypes[j] && sym->funcInfo->params[j].second && !typesEqual(argTypes[j], sym->funcInfo->params[j].second)) {
                        error(expr->line, "argument " + std::to_string(j + 1) + " of '" + callee->name + "': expected '" + typeToString(sym->funcInfo->params[j].second) + "', got '" + typeToString(argTypes[j]) + "'");
                    }
                }
            }
        }

        //  Возвращаем тип возврата функции
        if (sym->funcInfo)
            expr->resolvedType = sym->funcInfo->returnType;
        else
            expr->resolvedType = sym->type;
        return expr->resolvedType;
    }

    //  Доступ к полю структуры/класса (p.x)
    if (auto* field = dynamic_cast<FieldAccess*>(expr)) {
        auto objType = analyzeExpr(field->object);  //  Откуда мы пытаемся взять поле
        if (!objType) return nullptr;

        if (objType->kind == TypeKind::Struct) {    //  Если структура
            auto structSym = table.resolve(objType->name);  //  Ищем в таблице
            if (structSym && structSym->structInfo) {   //  Проходимся по вектору пар: имя -> тип поля
                for (auto& [fieldName, fieldType] : structSym->structInfo->fields) {
                    if (fieldName == field->field) {    //  Нашли поле
                        expr->resolvedType = fieldType;
                        return expr->resolvedType;
                    }
                }
                error(expr->line, "struct '" + objType->name + "' has no field '" + field->field + "'");
            }   //  Не нашли поле
            return nullptr;
        }

        if (objType->kind == TypeKind::Class) {     //  Если класс  
            auto classSym = table.resolve(objType->name);
            if (classSym && classSym->classInfo) {      //  Точно также ищем поля как в структуре
                for (auto& [fieldName, fieldType] : classSym->classInfo->fields) {
                    if (fieldName == field->field) {
                        expr->resolvedType = fieldType;
                        return expr->resolvedType;
                    }
                }   //  Если такого поля не нашли значит это возможно метод
                
                auto method = classSym->classInfo->methods.find(field->field);  //  Ищем метод
                if (method != classSym->classInfo->methods.end()) {
                    expr->resolvedType = method->second->returnType;
                    return expr->resolvedType;
                }
                error(expr->line, "class '" + objType->name + "' has no field or method '" + field->field + "'");
            }
            return nullptr;
        }

        error(expr->line, "field access on non-struct/class type '" + typeToString(objType) + "'");
        return nullptr;
    }

    //  Индексация массива arr[i]
    if (auto* arr = dynamic_cast<ArrayAccess*>(expr)) {
        auto objType = analyzeExpr(arr->object);    //  Здесь либо массив, либо строка
        auto indexType = analyzeExpr(arr->index);   //  Здесь число
        if (!objType) return nullptr;

        if (objType->kind != TypeKind::Array && objType->kind != TypeKind::DynArray && objType->kind != TypeKind::String) {
            error(expr->line, "index operator on non-array type '" + typeToString(objType) + "'");
            return nullptr;
        }

        if (indexType && !isIntType(indexType)) {   //  Индекс должен быть целым
            error(expr->line, "array index must be integer, got '" + typeToString(indexType) + "'");
        }

        if (objType->kind == TypeKind::String)
            expr->resolvedType = makeType(TypeKind::Char);  //  Элемент строки -> char
        else
            expr->resolvedType = objType->elementType;  //  Элемент массива -> определяем
        return expr->resolvedType;
    }

    //  Литерал массива [1, 2, 3] 
    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(expr)) {
        if (arrLit->elements.empty()) {     //  Если массив пуст
            error(expr->line, "cannot infer type of empty array literal");
            return nullptr;
        }

        auto firstType = analyzeExpr(arrLit->elements[0]);  //  Берём тип первого элемента как эталон
        for (size_t j = 1; j < arrLit->elements.size(); j++) {
            auto elemType = analyzeExpr(arrLit->elements[j]);
            if (firstType && elemType && !typesEqual(firstType, elemType)) {
                error(expr->line, "array element type mismatch: expected '" + typeToString(firstType) + "', got '" + typeToString(elemType) + "'");
            }
        }

        expr->resolvedType = makeArrayType(firstType, static_cast<int>(arrLit->elements.size()));
        return expr->resolvedType;
    }

    //  Литерал структуры Point { x: 5, y: 10 } 
    if (auto* structLit = dynamic_cast<StructLiteral*>(expr)) {
        auto sym = table.resolve(structLit->name);
        if (!sym || sym->kind != SymbolKind::Struct) {
            error(expr->line, "'" + structLit->name + "' is not a struct type");
            return nullptr;
        }

        if (sym->structInfo) {
            for (auto& init : structLit->fields) {  //  Проверяем каждое инициализируемое поле
                auto valType = analyzeExpr(init.value);
                bool found = false;
                for (auto& [fieldName, fieldType] : sym->structInfo->fields) {  //  Ищем поле с таким именем в определении структуры
                    if (fieldName == init.name) {
                        found = true;
                        if (valType && fieldType && !typesEqual(valType, fieldType)) {  //  Проверяем совместимость типов
                            error(expr->line, "field '" + init.name + "' of '" + structLit->name + "': expected '" + typeToString(fieldType) + "', got '" + typeToString(valType) + "'");
                        }
                        break;
                    }
                }
                if (!found){
                    error(expr->line, "struct '" + structLit->name + "' has no field '" + init.name + "'");
                }
            }
        }

        auto type = makeType(TypeKind::Struct);     //  Результат — тип этой структуры
        type->name = structLit->name;
        expr->resolvedType = type;
        return expr->resolvedType;
    }

    //  Явное приведение типа cast<T>(expr) 
    if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        auto fromType = analyzeExpr(cast->value);   //  Начальный тип
        auto targetType = resolveTypeName(cast->targetType);    //  Нужный тип
        if (!targetType) {
            error(expr->line, "unknown type '" + cast->targetType + "' in cast");
            return nullptr;
        }
        if (fromType && !isCastable(fromType, targetType)) {
            error(expr->line, "cannot cast '" + typeToString(fromType) + "' to '" + typeToString(targetType) + "'");
        }
        expr->resolvedType = targetType;
        return expr->resolvedType;
    }

    //  Доступ через namespace std::expected
    if (auto* namespace = dynamic_cast<NamespaceAccess*>(expr)) {
        auto namespaceSym = table.resolve(namespace->nameSpace);    //  Ищем namespace в таблице
        if (!namespaceSym) {
            error(expr->line, "'" + namespace->nameSpace + "' is not declared");
            return nullptr;
        }
        if (namespacesSym->kind != SymbolKind::Namespace || !namespaceSym->namespaceScope) {
            error(expr->line, "'" + ns->nameSpace + "' is not a namespace");
            return nullptr;
        }
        //  Ищем member внутри scope namespace
        auto it = namespaceSym->namespaceScope->symbols.find(namespace->member);
        if (it == namespaceSym->namespaceScope->symbols.end()) {
            error(expr->line, "'" + namespace->member + "' is not declared in namespace '" + namespace->nameSpace + "'");
            return nullptr;
        }
        expr->resolvedType = it->second->type;
        return expr->resolvedType;
    }

    //  Создание нового класса
    if (auto* newExpr = dynamic_cast<NewExpr*>(expr)) {
        auto classSym = table.resolve(newExpr->className);
        if (!classSym || classSym->kind != SymbolKind::Class) {
            error(expr->line, "'" + newExpr->className + "' is not a class");
            return nullptr;
        }

        //  Проверяем аргументы конструктора
        if (classSym->classInfo && classSym->classInfo->constructor) {
            auto& constructorParams = classSym->classInfo->constructor->params;
            if (newExpr->args.size() != constructorParams.size()) {
                error(expr->line, "constructor of '" + newExpr->className + "' expects " + std::to_string(constructorParams.size()) + " arguments, got " + std::to_string(newExpr->args.size()));
            }
            for (size_t j = 0; j < newExpr->args.size(); j++) {
                //  Пробрасываем тип параметра конструктора для контекстной типизации 
                auto expectedArg = (j < constructorParams.size()) ? constructorParams[j].second : nullptr;
                auto argType = analyzeExpr(newExpr->args[j], expectedArg);
                if (j < constructorParams.size() && argType && constructorParams[j].second) {
                    if (!isImplicitlyConvertible(argType, constructorParams[j].second)) {
                        error(expr->line, "constructor argument " + std::to_string(j + 1) + ": cannot convert '" + typeToString(argType) + "' to '" + typeToString(constructorParams[j].second) + "'");
                    }
                }
            }
        } 
        else {
            if (!newExpr->args.empty())     //  Нет конструктора — аргументов быть не должно
                error(expr->line, "class '" + newExpr->className + "' has no constructor");
            for (auto* arg : newExpr->args)
                analyzeExpr(arg);
        }

        auto type = makeType(TypeKind::Class);
        type->name = newExpr->className;
        expr->resolvedType = type;
        return expr->resolvedType;
    }

    return nullptr;
}

static bool alwaysReturns(Stmt* stmt) { //  Проверяет, что инструкция гарантированно завершается return
    if (!stmt) return false;
    if (dynamic_cast<Return*>(stmt)) {
        return true;
    }

    if (auto* block = dynamic_cast<Block*>(stmt)) { //  Блок возвращает, если его последняя инструкция возвращает
        if (block->statements.empty()) {
            return false;
        }
        return alwaysReturns(block->statements.back());
    }

    if (auto* ifStmt = dynamic_cast<If*>(stmt)) {   //  if/else возвращает, если обе ветки возвращают
        if (!ifStmt->elseBranch) {
            return false;
        }
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
        if (auto* func = dynamic_cast<FuncDecl*>(decl)) {   //  Функция
            auto sym = std::make_shared<Symbol>();
            sym->name = func->name;
            sym->kind = SymbolKind::Function;
            sym->type = resolveTypeName(func->returnType);

            sym->funcInfo = std::make_shared<FuncInfo>();   //  Заполняем информацию о сигнатуре
            sym->funcInfo->returnType = sym->type;
            for (auto& param : func->params) {
                auto paramType = resolveTypeName(param.typeName);
                if (!paramType) {
                    error(decl->line, "unknown parameter type '" + param.typeName + "' in function '" + func->name + "'");
                }
                sym->funcInfo->params.push_back({param.name, paramType});
            }

            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, result.error());
            }
        }
        
        else if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {  //  Структура
            auto sym = std::make_shared<Symbol>();
            sym->name = structDecl->name;
            sym->kind = SymbolKind::Struct;

            sym->structInfo = std::make_shared<StructInfo>();   //  Заполняем информацию о полях с их типами
            sym->structInfo->name = structDecl->name;
            for (auto& field : structDecl->fields) {
                auto fieldType = resolveTypeName(field.typeName);
                if (!fieldType)
                    error(decl->line, "unknown field type '" + field.typeName + "' in struct '" + structDecl->name + "'");
                sym->structInfo->fields.push_back({field.name, fieldType});
            }

            auto type = makeType(TypeKind::Struct);    //  Тип самой структуры — Struct с именем
            type->name = structDecl->name;
            sym->type = type;

            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, result.error());
            }
        }
        else if (auto* alias = dynamic_cast<TypeAlias*>(decl)) {    //  Type alias — регистрируем имя, разрешаем оригинальный тип
            auto sym = std::make_shared<Symbol>();
            sym->name = alias->alias;
            sym->kind = SymbolKind::TypeAlias;
            sym->type = resolveTypeName(alias->original);
            if (!sym->type) {
                error(decl->line, "unknown type '" + alias->original + "' in type alias '" + alias->alias + "'");
            }
            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, result.error());
            }
        }
        else if (auto* namespase = dynamic_cast<NamespaceDecl*>(decl)) {   //  Namespace 
            auto sym = std::make_shared<Symbol>();
            sym->name = namespase->name;
            sym->kind = SymbolKind::Namespace;

            auto result = table.declare(sym);
            if (!result) {
                error(decl->line, result.error());
            }

            table.enterScope(); //  новый namespace -> новая область видимости
            collectTopLevel(namespase->decls);  //  рекурсивно заполняем поля
            sym->namespaceScope = table.currentScope(); //  указатель на область видимости
            table.exitScope();  //  Выходим на обратный уровень
        }   
        else if (auto* clas = dynamic_cast<ClassDecl*>(decl)) {  //  Класс
            auto sym = std::make_shared<Symbol>();
            sym->name = clas->name;
            sym->kind = SymbolKind::Class;

            sym->classInfo = std::make_shared<ClassInfo>();
            sym->classInfo->name = clas->name;

            for (auto& field : clas->fields) {  //  Поля класса
                auto fieldType = resolveTypeName(field.typeName);
                if (!fieldType) {
                    error(decl->line, "unknown field type '" + field.typeName + "' in class '" + clas->name + "'");
                }
                sym->classInfo->fields.push_back({field.name, fieldType});
            }

            for (auto* method : clas->methods) { //  Методы класса
                auto methodInfo = std::make_shared<FuncInfo>();
                methodInfo->returnType = resolveTypeName(method->returnType);
                for (auto& param : method->params) {
                    auto paramType = resolveTypeName(param.typeName);
                    methodInfo->params.push_back({param.name, paramType});
                }
                sym->classInfo->methods[method->name] = methodInfo;
            }

            if (clas->constructor) {     //  Конструктор
                auto classInfo = std::make_shared<FuncInfo>();
                classInfo->returnType = makeType(TypeKind::Void);
                for (auto& param : clas->constructor->params) { //  Параметры конструктора
                    auto paramType = resolveTypeName(param.typeName);
                    classInfo->params.push_back({param.name, paramType});
                }
                sym->classInfo->constructor = classInfo;
            }

            sym->classInfo->hasDestructor = (clas->destructor != nullptr);   //  Деструктор

            auto type = makeType(TypeKind::Class);
            type->name = clas->name;
            sym->type = type;

            auto result = table.declare(sym);
            if (!result)
                error(decl->line, result.error());
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
            else if (auto* var = dynamic_cast<VarDecl*>(exp->decl))    exportedName = var->name;
            else if (auto* namespase = dynamic_cast<NamespaceDecl*>(exp->decl)) exportedName = namespase->name;

            if (!exportedName.empty()) {
                auto sym = table.resolve(exportedName);
                if (sym) sym->isExported = true;
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
    if (auto* var = dynamic_cast<VarDecl*>(stmt)) { //  Если это объявление переменной
        auto sym = std::make_shared<Symbol>();
        sym->name = var->name;
        sym->kind = SymbolKind::Variable;
        sym->isConst = var->isConst;
        sym->isInitialized = (var->init != nullptr);    //  Инициализовано или нет

        if (var->isAuto) {  //  auto — выводим тип из инициализатора (без контекста — литералы дают int32/float64)
            std::shared_ptr<Type> initType = analyzeExpr(var->init);
            if (!initType) {
                error(stmt->line, "'auto' requires initializer to infer type for '" + var->name + "'");
            }
            sym->type = initType;
        }
        else {  //  Явный тип — разрешаем имя типа
            sym->type = resolveTypeName(var->typeName);
            if (!sym->type) {
                error(stmt->line, "unknown type '" + var->typeName + "'");  //  Неизвестный тип
            }
            else if (sym->type->kind == TypeKind::Void) {   // void тип
                error(stmt->line, "cannot declare variable '" + var->name + "' with type 'void'");
            }

            //  Анализируем инициализатор, передавая тип переменной как ожидаемый
            std::shared_ptr<Type> initType = analyzeExpr(var->init, sym->type);

            //  Проверяем совместимость типа переменной и инициализатора
            //  Допускается widening: int32 x = int8_val — ок
            if (initType && sym->type) {
                if (!isImplicitlyConvertible(initType, sym->type)) {
                    error(stmt->line, "cannot initialize '" + var->name + "' of type '" + typeToString(sym->type) + "' with '" + typeToString(initType) + "'");
                }
            }
        }

        auto result = table.declare(sym);   //  Сохраняем в таблицу символов
        if (!result) {
            error(stmt->line, result.error());
        }s
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {    //  Если это блок то соответственно анализируем блок
        analyzeBlock(block);
    }
    else if (auto* assign = dynamic_cast<Assign*>(stmt)) {  //  Присваивание, проверяем типы, const, инициализацию
        auto targetType = analyzeExpr(assign->target);
        auto valueType = analyzeExpr(assign->value, targetType);    //  Тип target — ожидаемый для контекстной типизации литералов в правой части

        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            auto sym = table.resolve(id->name);     //  Находим инициализацию в таблице
            if (sym && sym->isConst) {   //  Если константная то не можем присвоить
                error(stmt->line, "cannot assign to const variable '" + id->name + "'");
            }
            if (sym) {
                sym->isInitialized = true; //  Помечаем переменную как инициализированную 
            }
        }

        if (targetType && valueType) {  //  Проверяем совместимость типов 
            if (!isImplicitlyConvertible(valueType, targetType)) {
                error(stmt->line, "type mismatch in assignment: cannot assign '" + typeToString(valueType) + "' to '" + typeToString(targetType) + "'");
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<If*>(stmt)) {  //  if
        auto condType = analyzeExpr(ifStmt->condition);
        if (condType && condType->kind != TypeKind::Bool) {   //  Условие всегда типа bool
            error(stmt->line, "if condition must be bool, got '" + typeToString(condType) + "'");
        }
        analyzeStmt(ifStmt->thenBranch);
        if (ifStmt->elseBranch) {
            analyzeStmt(ifStmt->elseBranch);
        }
    }
    else if (auto* whileStmt = dynamic_cast<While*>(stmt)) {  //  while 
        auto condType = analyzeExpr(whileStmt->condition);
        if (condType && condType->kind != TypeKind::Bool) { // условие также всегда должно быть bool
            error(stmt->line, "while condition must be bool, got '" + typeToString(condType) + "'");
        }
        loopDepth++;    //  Вошли в цикл на +1 глубину вложенности
        analyzeStmt(whileStmt->body);   //  Осмотрели чё там делается
        loopDepth--;    //  Вышли обратно
    }
    else if (dynamic_cast<Break*>(stmt)) {  //  Break
        if (loopDepth == 0) {   //  Break нельзя ставить на нулевой глубине, то бишь вне цикла
            error(stmt->line, "'break' outside of loop");   
        }
    }
    else if (dynamic_cast<Continue*>(stmt)) {
        if (loopDepth == 0) {   //  Continue тоже
            error(stmt->line, "'continue' outside of loop");
        }
    }
    else if (auto* ret = dynamic_cast<Return*>(stmt)) {  // Return
        if (ret->value) {
            auto valType = analyzeExpr(ret->value, currentReturnType);  //  Тип возврата функции — ожидаемый тип для контекстной типизации 
            if (valType && currentReturnType) {
                if (!isImplicitlyConvertible(valType, currentReturnType)) {
                    error(stmt->line, "return type mismatch: expected '" + typeToString(currentReturnType) + "', got '" + typeToString(valType) + "'");
                }
            }
        } 
        else {  //  return; без значения — функция должна быть void
            if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
                error(stmt->line, "return without value in non-void function");
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {  //  выражение как инструкция (напр. print(x);)
        analyzeExpr(exprStmt->expr);
    }
    else if (auto* func = dynamic_cast<FuncDecl*>(stmt)) {  //  Объявление функции
        auto sym = table.resolve(func->name);   //  Имя уже зарегистрировано в collectTopLevel — здесь только анализ тела
        auto prevReturnType = currentReturnType;    //  Сохраняем предыдущий тип возврата и ставим текущий (для проверки return)
        currentReturnType = (sym && sym->funcInfo) ? sym->funcInfo->returnType : nullptr;

        table.enterScope();     //  Тело функции — новый scope, в который добавляем параметры с типами
        if (sym && sym->funcInfo) {
            for (size_t j = 0; j < func->params.size(); j++) {  //  Регистрируем параметры
                auto paramSym = std::make_shared<Symbol>();
                paramSym->name = func->params[j].name;
                paramSym->kind = SymbolKind::Variable;
                paramSym->isInitialized = true;  //  параметры всегда инициализированы
                paramSym->type = (j < sym->funcInfo->params.size()) ? sym->funcInfo->params[j].second : nullptr;

                auto paramResult = table.declare(paramSym);
                if (!paramResult) {
                    error(stmt->line, paramResult.error());
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
                error(stmt->line, "function '" + func->name + "' does not return a value on all paths");
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
        selfieldType->name = clas->name;

        for (auto* method : clas->methods) {    //  Анализируем тела методов
            auto prevRetType = currentReturnType;
            auto currMethod = classSym->classInfo->methods.find(method->name);
            currentReturnType = (currMethod != classSym->classInfo->methods.end()) ? currMethod->second->returnType : nullptr;

            table.enterScope();  //  Добавляем self
            auto selfSym = std::make_shared<Symbol>();
            selfSym->name = "self";
            selfSym->kind = SymbolKind::Variable;
            selfSym->type = selfieldType;
            selfSym->isInitialized = true;
            table.declare(selfSym);

            //  Добавляем параметры
            if (currMethod != classSym->classInfo->methods.end()) {
                for (size_t j = 0; j < method->params.size(); j++) {
                    auto paramSym = std::make_shared<Symbol>();
                    paramSym->name = method->params[j].name;
                    paramSym->kind = SymbolKind::Variable;
                    paramSym->isInitialized = true;
                    paramSym->type = (j < currMethod->second->params.size()) ? currMethod->second->params[j].second : nullptr;
                    table.declare(paramSym);
                }
            }

            for (auto* stmt : method->body->statements) {
                analyzeStmt(stmt);
            }
            table.exitScope();

            if (currentReturnType && currentReturnType->kind != TypeKind::Void) {
                if (!alwaysReturns(method->body)) {
                    error(stmt->line, "method '" + cls->name + "." + method->name + "' does not return a value on all paths");
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
                    paramSym->type = (j < classSym->classInfo->constructor->params.size()) ? classSym->classInfo->constructor->params[j].second : nullptr;
                    table.declare(paramSym);
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
    }
    else if (auto* del = dynamic_cast<DeleteStmt*>(stmt)) {
        auto valType = analyzeExpr(del->value);
        if (valType && valType->kind != TypeKind::Class) {
            error(stmt->line, "delete requires a class instance, got '" + typeToString(valType) + "'");
        }
    }
    else if (auto* namespase = dynamic_cast<NamespaceDecl*>(stmt)) {    //  Входим в сохранённый scope и анализируем тела объявлений
        auto namespaceSym = table.resolve(namespase->name);
        if (namespaceSym && namespaceSym->namespaceScope) {
            table.pushScope(namespaceSym->namespaceScope);
            for (auto* decl : namespace->decls) {
                analyzeStmt(decl);
            }
            table.exitScope();
        }
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(stmt)) {  //  export — просто анализируем обёрнутое объявление
        analyzeStmt(exp->decl);
    }
}

//  Преобразование типа из libclang в тип MyLang.
//  Возвращает nullptr, если тип не поддерживается (struct/union по значению и т.п.).
static std::shared_ptr<Type> mapCType(CXType ct) {
    CXType canon = clang_getCanonicalType(ct);
    switch (canon.kind) {
        case CXType_Void:      return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Void;    return t; }();
        case CXType_Bool:      return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Bool;    return t; }();
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Char_U:
        case CXType_UChar:     return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Int8;    return t; }();
        case CXType_Short:     return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Int16;   return t; }();
        case CXType_UShort:    return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Uint16;  return t; }();
        case CXType_Int:       return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Int32;   return t; }();
        case CXType_UInt:      return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Uint32;  return t; }();
        case CXType_Long:
        case CXType_LongLong:  return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Int64;   return t; }();
        case CXType_ULong:
        case CXType_ULongLong: return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Uint64;  return t; }();
        case CXType_Float:     return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Float32; return t; }();
        case CXType_Double:    return [](){ auto t = std::make_shared<Type>(); t->kind = TypeKind::Float64; return t; }();
        case CXType_Pointer: {
            //  char*/const char* → String; любой другой указатель → Uint64 (сырой адрес)
            CXType pointee = clang_getCanonicalType(clang_getPointeeType(canon));
            if (pointee.kind == CXType_Char_S || pointee.kind == CXType_SChar
             || pointee.kind == CXType_Char_U || pointee.kind == CXType_UChar) {
                auto t = std::make_shared<Type>(); t->kind = TypeKind::String; return t;
            }
            auto t = std::make_shared<Type>(); t->kind = TypeKind::Uint64; return t;
        }
        default: return nullptr;
    }
}

void SemanticAnalyzer::processCImport(ImportDecl* imp) {
    //  Виртуальный TU: единственный файл с #include <header>
    std::string stub = "#include <" + imp->path + ">\n";
    const char* virtualName = "__mylang_cimport.c";

    CXUnsavedFile unsaved;
    unsaved.Filename = virtualName;
    unsaved.Contents = stub.c_str();
    unsaved.Length   = (unsigned long)stub.size();

    const char* args[] = {"-x", "c"};

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = nullptr;
    CXErrorCode ec = clang_parseTranslationUnit2(
        index, virtualName, args, 2, &unsaved, 1,
        CXTranslationUnit_SkipFunctionBodies, &tu);

    if (ec != CXError_Success || !tu) {
        error(imp->line, "libclang failed to parse header '" + imp->path + "'");
        clang_disposeIndex(index);
        return;
    }

    //  Для фильтрации — берём basename заголовка
    std::string wantedBase = std::filesystem::path(imp->path).filename().string();

    struct Ctx {
        std::string wantedBase;
        SymbolTable* table;
        int registered = 0;
        int skipped = 0;
    };
    Ctx ctx{wantedBase, &table, 0, 0};

    CXCursor root = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(root,
        [](CXCursor c, CXCursor, CXClientData data) -> CXChildVisitResult {
            auto* ctx = (Ctx*)data;
            if (clang_getCursorKind(c) != CXCursor_FunctionDecl)
                return CXChildVisit_Continue;

            CXSourceLocation loc = clang_getCursorLocation(c);
            CXFile file; unsigned line, col, off;
            clang_getFileLocation(loc, &file, &line, &col, &off);
            if (!file) return CXChildVisit_Continue;

            CXString fname = clang_getFileName(file);
            std::string base = std::filesystem::path(clang_getCString(fname)).filename().string();
            clang_disposeString(fname);
            if (base != ctx->wantedBase) return CXChildVisit_Continue;

            CXString nm = clang_getCursorSpelling(c);
            std::string funcName = clang_getCString(nm);
            clang_disposeString(nm);

            //  Маппим возвращаемый тип
            auto retType = mapCType(clang_getCursorResultType(c));
            if (!retType) { ctx->skipped++; return CXChildVisit_Continue; }

            //  Маппим параметры
            int nargs = clang_Cursor_getNumArguments(c);
            auto info = std::make_shared<FuncInfo>();
            info->returnType = retType;
            info->isExternC = true;
            info->isVariadic = clang_Cursor_isVariadic(c);

            bool ok = true;
            for (int i = 0; i < nargs; ++i) {
                CXCursor a = clang_Cursor_getArgument(c, i);
                auto pType = mapCType(clang_getCursorType(a));
                if (!pType) { ok = false; break; }
                CXString pn = clang_getCursorSpelling(a);
                info->params.push_back({clang_getCString(pn), pType});
                clang_disposeString(pn);
            }
            if (!ok) { ctx->skipped++; return CXChildVisit_Continue; }

            auto sym = std::make_shared<Symbol>();
            sym->name = funcName;
            sym->kind = SymbolKind::Function;
            sym->type = retType;
            sym->funcInfo = info;
            sym->isInitialized = true;

            if (ctx->table->declare(sym)) ctx->registered++;
            else ctx->skipped++;  //  имя уже занято (повторный импорт и т.п.)

            return CXChildVisit_Continue;
        }, &ctx);

    std::cerr << "[c-import] " << imp->path
              << ": registered " << ctx.registered
              << ", skipped " << ctx.skipped << "\n";

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
}

void SemanticAnalyzer::processImport(ImportDecl* imp) {
    if (imp->isC) {
        processCImport(imp);
        return;
    }

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
