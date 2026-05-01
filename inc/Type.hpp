#pragma once

#include <string>
#include <memory>

struct Expr;  // forward declaration для VLA-подобных размеров массивов

enum class TypeKind {
    Int8, Int16, Int32, Int64,
    Uint8, Uint16, Uint32, Uint64,
    Float32, Float64,
    Bool,
    Char,
    String,
    Void,
    Array,      // [T; N]
    DynArray,   // [T]
    Struct,
    Class,
    Alias,
    Null,       // тип литерала null, совместим с любым Class-типом
};

struct Type {
    TypeKind kind;

    // Array / DynArray — тип элемента
    std::shared_ptr<Type> elementType = nullptr;

    // Array — размер. -1 означает VLA (runtime-выражение в arraySizeExpr)
    int arraySize = -1;

    // Array — размер как runtime-выражение (T[N], где N произвольное выражение)
    Expr* arraySizeExpr = nullptr;

    // Позволяет различить статические массивы без указанного в момент компиляции размера
    int runtimeArrayId = 0;

    // Struct / Alias — имя
    std::string name;
};
