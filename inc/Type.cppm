module;

#include <cstdint>
#include <limits>
#include <string>
#include <memory>

export module types;

export enum class TypeKind {
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

export struct Type {
    TypeKind kind;

    // Array / DynArray — тип элемента
    std::shared_ptr<Type> elementType = nullptr;

    // Array — compile-time размер.
    int arraySize = -1;

    // Struct / Alias — имя
    std::string name;
};

export std::shared_ptr<Type> makeType(TypeKind kind) {
    auto type = std::make_shared<Type>();
    type->kind = kind;
    return type;
}

export std::shared_ptr<Type> makeArrayType(std::shared_ptr<Type> elem, int size) {
    auto type = makeType(TypeKind::Array);
    type->elementType = elem;
    type->arraySize = size;
    return type;
}

export std::shared_ptr<Type> makeDynArrayType(std::shared_ptr<Type> elem) {
    auto type = makeType(TypeKind::DynArray);
    type->elementType = elem;
    type->arraySize = -1;
    return type;
}

export bool typesEqual(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) {
        return false;
    }
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == TypeKind::Array) {
        return typesEqual(a->elementType, b->elementType) && a->arraySize == b->arraySize;
    }
    if (a->kind == TypeKind::DynArray) {
        return typesEqual(a->elementType, b->elementType);
    }
    if (a->kind == TypeKind::Struct || a->kind == TypeKind::Class || a->kind == TypeKind::Alias) {
        return a->name == b->name;
    }
    return true;
}

export std::string typeToString(const std::shared_ptr<Type>& type) {
    if (!type) return "<unknown>";

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
        case TypeKind::DynArray:
            return "<unknown>";
        case TypeKind::Struct:
        case TypeKind::Class:
        case TypeKind::Alias:
            return type->name;
        case TypeKind::Null:
            return "null";
    }

    return "<unknown>";
}

export bool isIntType(const std::shared_ptr<Type>& type) {
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

export bool isFloatType(const std::shared_ptr<Type>& type) {
    return type && (type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64);
}

export bool isNumericType(const std::shared_ptr<Type>& type) {
    return isIntType(type) || isFloatType(type);
}

export bool isIntegerLikeType(const std::shared_ptr<Type>& type) {
    return isIntType(type) || (type && (type->kind == TypeKind::Bool || type->kind == TypeKind::Char));
}

export bool isInputSupportedType(const std::shared_ptr<Type>& type) {
    if (!type) return false;

    if (type->kind == TypeKind::String || type->kind == TypeKind::Char || isIntType(type) || isFloatType(type)) {
        return true;
    }

    if (type->kind == TypeKind::Array || type->kind == TypeKind::DynArray) {
        return type->elementType && (type->elementType->kind == TypeKind::String
            || type->elementType->kind == TypeKind::Char
            || isIntType(type->elementType)
            || isFloatType(type->elementType));
    }

    return false;
}

export bool isInputArrayType(const std::shared_ptr<Type>& type) {
    if (!type || (type->kind != TypeKind::Array && type->kind != TypeKind::DynArray) || !type->elementType) {
        return false;
    }

    auto elem = type->elementType;
    return elem->kind == TypeKind::String
        || elem->kind == TypeKind::Char
        || isIntType(elem)
        || isFloatType(elem);
}

export int typeRank(const std::shared_ptr<Type>& type) {
    if (!type) return 0;

    switch (type->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8:   return 1;
        case TypeKind::Int16:
        case TypeKind::Uint16:  return 2;
        case TypeKind::Int32:
        case TypeKind::Uint32:  return 3;
        case TypeKind::Int64:
        case TypeKind::Uint64:  return 4;
        case TypeKind::Float32: return 5;
        case TypeKind::Float64: return 6;
        default:                return 0;
    }
}

export bool isSignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Int8 && type->kind <= TypeKind::Int64;
}

export bool isUnsignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Uint8 && type->kind <= TypeKind::Uint64;
}

export int intBitWidth(const std::shared_ptr<Type>& type) {
    if (!type) return 0;

    switch (type->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8:  return 8;
        case TypeKind::Int16:
        case TypeKind::Uint16: return 16;
        case TypeKind::Int32:
        case TypeKind::Uint32: return 32;
        case TypeKind::Int64:
        case TypeKind::Uint64: return 64;
        default:               return 0;
    }
}

export TypeKind signedIntKindForBits(int bits) {
    if (bits <= 8) return TypeKind::Int8;
    if (bits <= 16) return TypeKind::Int16;
    if (bits <= 32) return TypeKind::Int32;
    return TypeKind::Int64;
}

export bool isImplicitlyConvertible(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;

    if (typesEqual(from, to)) return true;

    if (isSignedIntType(from) && isSignedIntType(to)) {
        return typeRank(from) <= typeRank(to);
    }

    if (isUnsignedIntType(from) && isUnsignedIntType(to)) {
        return typeRank(from) <= typeRank(to);
    }

    if (from->kind == TypeKind::Float32 && to->kind == TypeKind::Float64) {
        return true;
    }

    if (isIntType(from) && isFloatType(to)) {
        return true;
    }

    if (from->kind == TypeKind::Array && to->kind == TypeKind::DynArray) {
        return from->elementType && to->elementType
            && isImplicitlyConvertible(from->elementType, to->elementType);
    }

    if (from->kind == TypeKind::Null) {
        return to->kind == TypeKind::Class;
    }

    return false;
}

export bool isUnsupportedEqualityType(const std::shared_ptr<Type>& type) {
    return !type
        || type->kind == TypeKind::Struct
        || type->kind == TypeKind::Array
        || type->kind == TypeKind::DynArray;
}

export bool integerLiteralFitsType(double value, const std::shared_ptr<Type>& type) {
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
            return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<uint8_t>::max());
        case TypeKind::Uint16:
            return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<uint16_t>::max());
        case TypeKind::Uint32:
            return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<uint32_t>::max());
        case TypeKind::Uint64:
            return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<uint64_t>::max());
        case TypeKind::Char:
            return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<unsigned char>::max());
        default:
            return true;
    }
}

export bool isCastable(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!from || !to) return false;

    if (typesEqual(from, to)) return true;

    bool fromInt = isIntType(from);
    bool toInt = isIntType(to);
    bool fromFloat = isFloatType(from);
    bool toFloat = isFloatType(to);

    if ((fromInt || fromFloat) && (toInt || toFloat)) {
        return true;
    }

    if (fromInt && to->kind == TypeKind::Bool) return true;
    if (from->kind == TypeKind::Bool && toInt) return true;
    if (from->kind == TypeKind::Char && toInt) return true;
    if (fromInt && to->kind == TypeKind::Char) return true;
    if (from->kind == TypeKind::String && to->kind == TypeKind::Bool) return true;
    if (from->kind == TypeKind::Bool && to->kind == TypeKind::String) return true;

    return false;
}

export std::shared_ptr<Type> commonType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!a || !b) return nullptr;

    if (typesEqual(a, b)) return a;

    if ((isIntType(a) && isFloatType(b)) || (isFloatType(a) && isIntType(b))) {
        if (a->kind == TypeKind::Float64 || b->kind == TypeKind::Float64) {
            return makeType(TypeKind::Float64);
        }
        return makeType(TypeKind::Float32);
    }

    if (isFloatType(a) && isFloatType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

    if (isSignedIntType(a) && isSignedIntType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

    if (isUnsignedIntType(a) && isUnsignedIntType(b)) {
        if (typeRank(a) >= typeRank(b)) return a;
        return b;
    }

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

    return nullptr;
}

export int inputTypeCode(const std::shared_ptr<Type>& type) {
    if (!type) return 0;

    switch (type->kind) {
        case TypeKind::Int8:    return 1;
        case TypeKind::Int16:   return 2;
        case TypeKind::Int32:   return 3;
        case TypeKind::Int64:   return 4;
        case TypeKind::Uint8:   return 5;
        case TypeKind::Uint16:  return 6;
        case TypeKind::Uint32:  return 7;
        case TypeKind::Uint64:  return 8;
        case TypeKind::Float32: return 9;
        case TypeKind::Float64: return 10;
        case TypeKind::String:  return 11;
        case TypeKind::Char:    return 12;
        default:                return 0;
    }
}

export int dynArrayElemSize(const std::shared_ptr<Type>& elemType) {
    if (!elemType) return 8;

    switch (elemType->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8:
        case TypeKind::Bool:
        case TypeKind::Char:
            return 1;
        case TypeKind::Int16:
        case TypeKind::Uint16:
            return 2;
        case TypeKind::Int32:
        case TypeKind::Uint32:
        case TypeKind::Float32:
            return 4;
        default:
            return 8;
    }
}
