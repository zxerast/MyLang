#pragma once

#include <string>
#include <memory>

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

    // Array — compile-time размер.
    int arraySize = -1;

    // Struct / Alias — имя
    std::string name;
};
