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
    Array,      // T[N]
    DynArray,   // T[]
    Struct,
    Class,
    Alias,
};

struct Type {
    TypeKind kind;
    std::shared_ptr<Type> elementType = nullptr;    // Array / DynArray — тип элемента
    int arraySize = -1;     // Array — размер
    std::string name;       // Struct / Alias — имя
};
