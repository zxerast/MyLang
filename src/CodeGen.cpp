#include "CodeGen.hpp"
#include "SymbolTable.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <fstream>

std::string CodeGen::newLabel(const std::string& hint) {
    return "." + hint + std::to_string(labelCounter++);     //  Установка метки
}

std::string CodeGen::internString(const std::string& str) { //  Заполнение строк в rodata
    auto it = stringPool.find(str);
    if (it != stringPool.end()) return it->second;

    std::string label = "str" + std::to_string(stringCounter++);    //  Ставим метку
    stringPool[str] = label;

    rodata << label << ": db `";    //  Любая строка это double-word
    for (unsigned char c : str) {
        if (c == '`' || c == '\\') rodata << '\\' << c;
        else if (c == '\n') rodata << "\\n";
        else if (c == '\r') rodata << "\\r";
        else if (c == '\t') rodata << "\\t";
        else if (c < ' ' || c > '~') rodata << "`, " << (int)c << ", `";
        else rodata << c;
    }
    rodata << "`, 0\n";
    return label;
}

int CodeGen::sizeOfType(const std::shared_ptr<Type>& type) const{    //  Возвращает размер типа
    if (!type) return 8;
    switch (type->kind) {
        case TypeKind::Int8:  
        case TypeKind::Uint8:  
        case TypeKind::Bool:  
        case TypeKind::Char: return 1;

        case TypeKind::Int16: 
        case TypeKind::Uint16: return 2;

        case TypeKind::Int32: 
        case TypeKind::Uint32: 
        case TypeKind::Float32: return 4;

        case TypeKind::Int64: 
        case TypeKind::Uint64: 
        case TypeKind::Float64: 
        case TypeKind::String: 
        case TypeKind::Class:
        case TypeKind::Null: return 8;  //  указатель

        case TypeKind::DynArray:
            return 24; // ptr + len + cap

        case TypeKind::Array: {
            int n = type->arraySize;
            if (n < 0) return 0; // минимальный fallback для runtime-sized array
            return sizeOfType(type->elementType) * n;
        }

        case TypeKind::Struct: {
            auto it = structSizes.find(type->name);
            if (it != structSizes.end()) return it->second;
            return 8;
        }

        case TypeKind::Alias:
            return 8;

        case TypeKind::Void:
            return 0; 
    }
    return 8;
}

int CodeGen::allocLocal(const std::shared_ptr<Symbol>& sym, const std::string& nameHint, const std::shared_ptr<Type>& type) {
    (void)nameHint;

    int size = sizeOfType(type);

    if (size < 8) {
        size = 8;
    }

    int align = size;

    if (align > 8) {
        align = 8;
    }

    int rem = currentFrameSize % align;

    if (rem != 0) {
        currentFrameSize += align - rem;
    }

    currentFrameSize += size;

    int offset = -currentFrameSize;

    LocalVar local{offset, type};

    if (sym) {
        localsBySymbol[sym] = local;
    }

    return offset;
}

const LocalVar* CodeGen::findLocal(const std::shared_ptr<Symbol>& sym) const {
    if (!sym) {
        return nullptr;
    }

    auto it = localsBySymbol.find(sym);

    if (it == localsBySymbol.end()) {
        return nullptr;
    }

    return &it->second;
}

static std::string ptrSuffix(const std::shared_ptr<Symbol>& sym) {
    if (!sym) {
        return "0";
    }

    std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(sym.get());

    std::ostringstream ss;
    ss << std::hex << raw;
    return ss.str();
}

//  Инициализирует слот глобалки в прологе main. Логика зеркалит локальный VarDecl,
//  но пишет в .bss-лейбл глобального символа вместо стэковой локалки.
void CodeGen::compileGlobalInit(VarDecl* decl, VarInit* var) {
    auto type = varInitType(var, decl);
    std::string slot = globalLabel(var->resolvedSym, var->name);

    if (!type) {
        return;
    }

    // Без инициализатора: default прямо в глобальный слот.
    if (!var->init) {
        text << "    lea rdi, [rel " << slot << "]\n";
        emitDefault("rdi", type);
        return;
    }

    // Составные значения: rhs expression возвращает адрес source-объекта.
    // В глобальный слот надо скопировать значение, а не положить адрес.
    if (isCompositeMemoryType(type)) {
        compileExpr(var->init);                 // rax = src address
        text << "    mov rsi, rax\n";
        text << "    lea rdi, [rel " << slot << "]\n";
        emitCopy("rdi", "rsi", type);
        return;
    }

    // Обычные скаляры.
    compileExprAs(var->init, type);
    emitStore("[rel " + slot + "]", type);
}

bool CodeGen::isDynArrayTypeName(TypeName* name) {
    if (!name || name->suffixes.empty()) return false;
    return name->suffixes.back().isDynamic;
}

bool CodeGen::isFloatTypeName(TypeName* name) {
    if (!name || !name->suffixes.empty()) return false;
    return name->base == "float" || name->base == "float32" || name->base == "float64";
}

std::shared_ptr<Type> CodeGen::exprType(Expr* e) const {
    if (!e) {
        return nullptr;
    }

    // Главный источник истины: тип, проставленный семантикой.
    if (e->resolvedType) {
        return e->resolvedType;
    }

    if (auto* id = dynamic_cast<Identifier*>(e)) {
        if (id->resolvedSym && id->resolvedSym->type) {
            return id->resolvedSym->type;
        }

        if (id->resolvedField && id->resolvedField->type) {
            return id->resolvedField->type;
        }

        return nullptr;
    }

    if (auto* ns = dynamic_cast<NamespaceAccess*>(e)) {
        if (ns->resolvedSym && ns->resolvedSym->type) {
            return ns->resolvedSym->type;
        }

        if (ns->resolvedSym && ns->resolvedSym->funcInfo) {
            return ns->resolvedSym->funcInfo->returnType;
        }

        return nullptr;
    }

    if (auto* field = dynamic_cast<FieldAccess*>(e)) {
        return fieldType(field);
    }

    if (auto* call = dynamic_cast<FuncCall*>(e)) {
        return callReturnType(call);
    }

    if (auto* arr = dynamic_cast<ArrayAccess*>(e)) {
        auto objType = exprType(arr->object);

        if (objType && (objType->kind == TypeKind::Array || objType->kind == TypeKind::DynArray)) {
            return objType->elementType;
        }

        if (objType && objType->kind == TypeKind::String) {
            auto t = std::make_shared<Type>();
            t->kind = TypeKind::Char;
            return t;
        }

        return nullptr;
    }

    // ВАЖНО:
    // CastExpr, StructLiteral, ArrayLiteral, Binary, Unary и т.д. не реконструируем здесь.
    // Их тип обязан быть в e->resolvedType после семантики.
    return nullptr;
}

std::shared_ptr<Type> CodeGen::varInitType(VarInit* v, VarDecl* decl) const {
    (void)decl;

    if (!v) {
        return nullptr;
    }

    // Для переменной главный источник — её семантический символ.
    // Это покрывает explicit type, auto, aliases, namespace-shadowing, runtime-sized arrays.
    if (v->resolvedSym && v->resolvedSym->type) {
        return v->resolvedSym->type;
    }

    // Аварийный fallback только для уже аннотированного initializer.
    // Не заменяет тип объявления, но позволяет не падать на частично старом AST.
    if (v->init && v->init->resolvedType) {
        return v->init->resolvedType;
    }

    return nullptr;
}

std::shared_ptr<Type> CodeGen::paramType(const Param& p) const {
    if (p.resolvedSym && p.resolvedSym->type) {
        return p.resolvedSym->type;
    }

    if (p.defaultValue && p.defaultValue->resolvedType) {
        return p.defaultValue->resolvedType;
    }

    return nullptr;
}

std::shared_ptr<Type> CodeGen::paramType(FuncDecl* func, size_t index) const {
    if (!func) {
        return nullptr;
    }

    // Для сигнатуры функции/метода главный источник — resolvedInfo.
    if (func->resolvedInfo && index < func->resolvedInfo->params.size()) {
        return func->resolvedInfo->params[index].type;
    }

    if (func->resolvedSym && func->resolvedSym->funcInfo &&
        index < func->resolvedSym->funcInfo->params.size()) {
        return func->resolvedSym->funcInfo->params[index].type;
    }

    if (index < func->params.size()) {
        return paramType(func->params[index]);
    }

    return nullptr;
}

std::shared_ptr<Type> CodeGen::returnType(FuncDecl* func) const {
    if (!func) {
        return nullptr;
    }

    if (func->resolvedInfo && func->resolvedInfo->returnType) {
        return func->resolvedInfo->returnType;
    }

    if (func->resolvedSym && func->resolvedSym->funcInfo &&
        func->resolvedSym->funcInfo->returnType) {
        return func->resolvedSym->funcInfo->returnType;
    }

    if (func->resolvedSym && func->resolvedSym->type) {
        return func->resolvedSym->type;
    }

    return nullptr;
}

std::shared_ptr<Type> CodeGen::fieldType(FieldAccess* f) const {
    if (!f) {
        return nullptr;
    }

    if (f->resolvedField && f->resolvedField->type) {
        return f->resolvedField->type;
    }

    if (f->resolvedType) {
        return f->resolvedType;
    }

    return nullptr;
}

std::shared_ptr<FuncInfo> CodeGen::callFuncInfo(FuncCall* call) const {
    if (!call) {
        return nullptr;
    }

    if (call->resolvedMethod) {
        return call->resolvedMethod;
    }

    if (call->resolvedCallee && call->resolvedCallee->funcInfo) {
        return call->resolvedCallee->funcInfo;
    }

    return nullptr;
}

std::shared_ptr<Type> CodeGen::callReturnType(FuncCall* call) const {
    if (!call) {
        return nullptr;
    }

    if (call->resolvedType) {
        return call->resolvedType;
    }

    if (auto info = callFuncInfo(call)) {
        return info->returnType;
    }

    if (call->resolvedCallee && call->resolvedCallee->type) {
        return call->resolvedCallee->type;
    }

    return nullptr;
}

std::string CodeGen::mangleQualifiedName(const std::string& name) const {
    std::string result;
    result.reserve(name.size());

    for (size_t i = 0; i < name.size(); ++i) {
        if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':') {
            result += "__";
            ++i;
        }
        else {
            result += name[i];
        }
    }

    return result;
}

std::string CodeGen::mangleNamespaceAccess(const NamespaceAccess* access) const {
    if (!access) return "";
    return mangleQualifiedName(access->nameSpace + "::" + access->member);
}

std::string CodeGen::symbolLabel(const std::shared_ptr<Symbol>& sym, const std::string& fallbackName) const {
    if (sym && !sym->name.empty()) {
        bool isBuiltin =
            sym->name == "print" || sym->name == "input" || sym->name == "len" ||
            sym->name == "exit" || sym->name == "panic" ||
            sym->name == "push" || sym->name == "pop";
        bool alreadyQualified = sym->name.find("::") != std::string::npos;
        if (sym->kind == SymbolKind::Function && !isBuiltin && !alreadyQualified && !currentNamespace.empty()) {
            return mangleQualifiedName(currentNamespace + "::" + sym->name);
        }
        return mangleQualifiedName(sym->name);
    }
    return mangleQualifiedName(fallbackName);
}

int CodeGen::storageSizeOfType(const std::shared_ptr<Type>& type) const {
    if (!type) return 8;

    return sizeOfType(type);
}

bool CodeGen::isSignedIntType(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    return type->kind == TypeKind::Int8
        || type->kind == TypeKind::Int16
        || type->kind == TypeKind::Int32
        || type->kind == TypeKind::Int64;
}

bool CodeGen::isUnsignedIntType(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    return type->kind == TypeKind::Uint8
        || type->kind == TypeKind::Uint16
        || type->kind == TypeKind::Uint32
        || type->kind == TypeKind::Uint64;
}

bool CodeGen::isIntegerLikeType(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    return isSignedIntType(type)
        || isUnsignedIntType(type)
        || type->kind == TypeKind::Bool
        || type->kind == TypeKind::Char;
}

bool CodeGen::isFloatType(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    return type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64;
}

static bool isCodegenNumericType(const std::shared_ptr<Type>& type) {
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
        case TypeKind::Bool:
        case TypeKind::Char:
        case TypeKind::Float32:
        case TypeKind::Float64:
            return true;
        default:
            return false;
    }
}

static bool isCodegenFloatType(const std::shared_ptr<Type>& type) {
    return type && (type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64);
}

static bool isCodegenIntType(const std::shared_ptr<Type>& type) {
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

static bool isCodegenSignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Int8 && type->kind <= TypeKind::Int64;
}

static bool isCodegenUnsignedIntType(const std::shared_ptr<Type>& type) {
    return type && type->kind >= TypeKind::Uint8 && type->kind <= TypeKind::Uint64;
}

static int codegenIntBitWidth(const std::shared_ptr<Type>& type) {
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

static TypeKind codegenSignedIntKindForBits(int bits) {
    if (bits <= 8) return TypeKind::Int8;
    if (bits <= 16) return TypeKind::Int16;
    if (bits <= 32) return TypeKind::Int32;
    return TypeKind::Int64;
}

static bool isCodegenInputArrayType(const std::shared_ptr<Type>& type) {
    if (!type || (type->kind != TypeKind::Array && type->kind != TypeKind::DynArray) || !type->elementType) {
        return false;
    }

    auto elem = type->elementType;
    return elem->kind == TypeKind::String
        || elem->kind == TypeKind::Char
        || isCodegenIntType(elem)
        || isCodegenFloatType(elem);
}

static int codegenInputTypeCode(const std::shared_ptr<Type>& type) {
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

static int codegenDynArrayElemSize(const std::shared_ptr<Type>& elemType) {
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

static std::string shellQuote(const std::string& value) {
    std::string result = "'";

    for (char ch : value) {
        if (ch == '\'') {
            result += "'\\''";
        }
        else {
            result += ch;
        }
    }

    result += "'";
    return result;
}

static std::string codegenQualifiedName(const std::string& prefix, const std::string& name) {
    if (prefix.empty()) {
        return name;
    }

    return prefix + "::" + name;
}

static std::shared_ptr<Type> makeCodegenScalarType(TypeKind kind) {
    auto type = std::make_shared<Type>();
    type->kind = kind;
    return type;
}

static std::shared_ptr<Type> codegenCommonNumericType(const std::shared_ptr<Type>& a, const std::shared_ptr<Type>& b) {
    if (!isCodegenNumericType(a) || !isCodegenNumericType(b)) return nullptr;

    if (isCodegenFloatType(a) || isCodegenFloatType(b)) {
        if ((a && a->kind == TypeKind::Float64) || (b && b->kind == TypeKind::Float64)) {
            return makeCodegenScalarType(TypeKind::Float64);
        }
        return makeCodegenScalarType(TypeKind::Float32);
    }

    if (isCodegenSignedIntType(a) && isCodegenSignedIntType(b)) {
        return codegenIntBitWidth(a) >= codegenIntBitWidth(b) ? a : b;
    }

    if (isCodegenUnsignedIntType(a) && isCodegenUnsignedIntType(b)) {
        return codegenIntBitWidth(a) >= codegenIntBitWidth(b) ? a : b;
    }

    if ((isCodegenSignedIntType(a) && isCodegenUnsignedIntType(b)) ||
        (isCodegenUnsignedIntType(a) && isCodegenSignedIntType(b))) {
        auto signedType = isCodegenSignedIntType(a) ? a : b;
        auto unsignedType = isCodegenUnsignedIntType(a) ? a : b;
        int signedBits = codegenIntBitWidth(signedType);
        int unsignedBits = codegenIntBitWidth(unsignedType);
        int commonBits = signedBits;

        if (signedBits <= unsignedBits) {
            commonBits = unsignedBits * 2;
        }

        if (commonBits <= 64) {
            return makeCodegenScalarType(codegenSignedIntKindForBits(commonBits));
        }
        return makeCodegenScalarType(TypeKind::Float64);
    }

    return a;
}

bool CodeGen::isCompositeMemoryType(const std::shared_ptr<Type>& type) {
    if (!type) return false;
    
    return type->kind == TypeKind::Array
        || type->kind == TypeKind::DynArray
        || type->kind == TypeKind::Struct;
}

void CodeGen::emitNullCheck(const std::string& reg, int line) {
    std::string okLabel = newLabel("nonnull");
    text << "    test " << reg << ", " << reg << "\n";
    text << "    jnz " << okLabel << "\n";
    text << "    lea rdi, [rel __rt_null_object]\n";
    text << "    mov rsi, " << line << "\n";
    text << "    call lang_panic\n";
    text << okLabel << ":\n";
}

void CodeGen::emitNormalizeRax(const std::shared_ptr<Type>& type) {
    if (!type) return;

    switch (type->kind) {
        case TypeKind::Int8:
            text << "    movsx rax, al\n";
            break;

        case TypeKind::Uint8:
        case TypeKind::Char:
            text << "    movzx rax, al\n";
            break;

        case TypeKind::Bool:
            text << "    test rax, rax\n";
            text << "    setne al\n";
            text << "    movzx rax, al\n";
            break;

        case TypeKind::Int16:
            text << "    movsx rax, ax\n";
            break;

        case TypeKind::Uint16:
            text << "    movzx rax, ax\n";
            break;

        case TypeKind::Int32:
            text << "    movsxd rax, eax\n";
            break;

        case TypeKind::Uint32:
            text << "    mov eax, eax\n";
            break;

        case TypeKind::Int64:
        case TypeKind::Uint64:
        case TypeKind::String:
        case TypeKind::Class:
        case TypeKind::Struct:
        case TypeKind::Null:
        case TypeKind::Float32:
        case TypeKind::Float64:
        case TypeKind::Array:
        case TypeKind::DynArray:
        case TypeKind::Alias:
        case TypeKind::Void:
            break;
    }
}

void CodeGen::emitCastFromTo(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to) {
    if (!to) return;
    if (!from) {
        emitNormalizeRax(to);
        return;
    }

    if (from->kind == to->kind) {
        emitNormalizeRax(to);
        return;
    }

    bool fromFloat = isFloatType(from);
    bool toFloat   = isFloatType(to);
    bool fromInt   = isIntegerLikeType(from);
    bool toInt     = isIntegerLikeType(to);

    // float32 -> float64
    if (from->kind == TypeKind::Float32 && to->kind == TypeKind::Float64) {
        text << "    movd xmm0, eax\n";
        text << "    cvtss2sd xmm0, xmm0\n";
        text << "    movq rax, xmm0\n";
        return;
    }

    // float64 -> float32
    if (from->kind == TypeKind::Float64 && to->kind == TypeKind::Float32) {
        text << "    movq xmm0, rax\n";
        text << "    cvtsd2ss xmm0, xmm0\n";
        text << "    movd eax, xmm0\n";
        return;
    }

    // int/bool/char -> float
    if (fromInt && toFloat) {
        emitNormalizeRax(from);

        if (from->kind == TypeKind::Uint64) {
            std::string normalLabel = newLabel("u64_float_normal");
            std::string endLabel = newLabel("u64_float_end");

            text << "    test rax, rax\n";
            text << "    jns " << normalLabel << "\n";
            text << "    mov rbx, rax\n";
            text << "    and rax, 1\n";
            text << "    shr rbx, 1\n";
            text << "    or rbx, rax\n";
            if (to->kind == TypeKind::Float32) {
                text << "    cvtsi2ss xmm0, rbx\n";
                text << "    addss xmm0, xmm0\n";
                text << "    movd eax, xmm0\n";
            }
            else {
                text << "    cvtsi2sd xmm0, rbx\n";
                text << "    addsd xmm0, xmm0\n";
                text << "    movq rax, xmm0\n";
            }
            text << "    jmp " << endLabel << "\n";
            text << normalLabel << ":\n";
            if (to->kind == TypeKind::Float32) {
                text << "    cvtsi2ss xmm0, rax\n";
                text << "    movd eax, xmm0\n";
            }
            else {
                text << "    cvtsi2sd xmm0, rax\n";
                text << "    movq rax, xmm0\n";
            }
            text << endLabel << ":\n";
            return;
        }

        if (to->kind == TypeKind::Float32) {
            text << "    cvtsi2ss xmm0, rax\n";
            text << "    movd eax, xmm0\n";
        }
        else {
            text << "    cvtsi2sd xmm0, rax\n";
            text << "    movq rax, xmm0\n";
        }
        return;
    }

    // float -> int/bool/char
    if (fromFloat && toInt) {
        if (from->kind == TypeKind::Float32) {
            text << "    movd xmm0, eax\n";
            text << "    cvttss2si rax, xmm0\n";
        }
        else {
            text << "    movq xmm0, rax\n";
            text << "    cvttsd2si rax, xmm0\n";
        }

        emitNormalizeRax(to);
        return;
    }

    // int/bool/char -> int/bool/char
    if (fromInt && toInt) {
        emitNormalizeRax(to);
        return;
    }

    // null -> class
    if (from->kind == TypeKind::Null && to->kind == TypeKind::Class) {
        text << "    xor rax, rax\n";
        return;
    }

    // Остальное семантика уже должна была запретить.
    emitNormalizeRax(to);
}

void CodeGen::emitLoad(const std::string& addr, const std::shared_ptr<Type>& type) {
    if (!type) {
        text << "    mov rax, qword " << addr << "\n";
        return;
    }

    switch (type->kind) {
        case TypeKind::Int8:
            text << "    movsx rax, byte " << addr << "\n";
            break;

        case TypeKind::Uint8:
        case TypeKind::Bool:
        case TypeKind::Char:
            text << "    movzx rax, byte " << addr << "\n";
            break;

        case TypeKind::Int16:
            text << "    movsx rax, word " << addr << "\n";
            break;

        case TypeKind::Uint16:
            text << "    movzx rax, word " << addr << "\n";
            break;

        case TypeKind::Int32:
            text << "    movsxd rax, dword " << addr << "\n";
            break;

        case TypeKind::Uint32:
        case TypeKind::Float32:
            text << "    mov eax, dword " << addr << "\n";
            break;

        case TypeKind::Int64:
        case TypeKind::Uint64:
        case TypeKind::Float64:
        case TypeKind::String:
        case TypeKind::Class:
        case TypeKind::Null:
            text << "    mov rax, qword " << addr << "\n";
            break;
        
        case TypeKind::Struct:
        case TypeKind::Array:
        case TypeKind::DynArray:
            // Массив как expression должен возвращать адрес, а не грузиться как qword.
            // Поэтому сюда лучше не попадать.
            text << "    ; emitLoad skipped for memory aggregate\n";
            break;

        case TypeKind::Alias:
            text << "    mov rax, qword " << addr << "\n";
            break;

        case TypeKind::Void:
            break;
    }
}

void CodeGen::emitStore(const std::string& addr, const std::shared_ptr<Type>& type) {
    if (!type) {
        text << "    mov qword " << addr << ", rax\n";
        return;
    }

    emitNormalizeRax(type);

    switch (type->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8:
        case TypeKind::Bool:
        case TypeKind::Char:
            text << "    mov byte " << addr << ", al\n";
            break;

        case TypeKind::Int16:
        case TypeKind::Uint16:
            text << "    mov word " << addr << ", ax\n";
            break;

        case TypeKind::Int32:
        case TypeKind::Uint32:
        case TypeKind::Float32:
            text << "    mov dword " << addr << ", eax\n";
            break;

        case TypeKind::Int64:
        case TypeKind::Uint64:
        case TypeKind::Float64:
        case TypeKind::String:
        case TypeKind::Class:
        case TypeKind::Null:
        case TypeKind::Alias:
            text << "    mov qword " << addr << ", rax\n";
            break;
        
        case TypeKind::Struct:
        case TypeKind::Array:
        case TypeKind::DynArray:
            text << "    ; emitStore skipped for memory aggregate\n";
            break;

        case TypeKind::Void:
            break;
    }
}

void CodeGen::emitCopy(const std::string& dstReg, const std::string& srcReg, const std::shared_ptr<Type>& type) {
    if (!type) return;

    int size = sizeOfType(type);
    if (size <= 0) return;

    // class object copy: caller passes object pointers, not pointer-slot addresses.
    if (type->kind == TypeKind::Class) {
        auto orderIt = structFieldOrder.find(type->name);
        auto layoutIt = structLayouts.find(type->name);
        auto typeIt = structFieldTypes.find(type->name);

        if (orderIt != structFieldOrder.end() &&
            layoutIt != structLayouts.end() &&
            typeIt != structFieldTypes.end()) {

            text << "    push " << dstReg << "\n";
            text << "    push " << srcReg << "\n";

            for (const auto& fieldName : orderIt->second) {
                int off = layoutIt->second[fieldName];
                auto fieldType = typeIt->second[fieldName];

                text << "    mov rdi, [rsp + 8]\n";
                if (off != 0) text << "    add rdi, " << off << "\n";

                text << "    mov rsi, [rsp]\n";
                if (off != 0) text << "    add rsi, " << off << "\n";

                emitCopy("rdi", "rsi", fieldType);
            }

            text << "    add rsp, 16\n";
            return;
        }
    }

    // scalar copy
    if (!isCompositeMemoryType(type) && size <= 8) {
        emitLoad("[" + srcReg + "]", type);
        emitStore("[" + dstReg + "]", type);
        return;
    }

    // fixed array: value-copy every element
    if (type->kind == TypeKind::Array) {
        int n = type->arraySize;
        if (n < 0) {
            // runtime-sized fixed arrays пока безопасно копируем как указатель/fallback
            text << "    ; unsupported value-copy of runtime-sized fixed array\n";
            return;
        }

        int elemSize = sizeOfType(type->elementType);

        text << "    push " << dstReg << "\n";
        text << "    push " << srcReg << "\n";

        for (int i = 0; i < n; ++i) {
            int off = i * elemSize;

            text << "    mov rdi, [rsp + 8]\n";
            if (off != 0) text << "    add rdi, " << off << "\n";

            text << "    mov rsi, [rsp]\n";
            if (off != 0) text << "    add rsi, " << off << "\n";

            emitCopy("rdi", "rsi", type->elementType);
        }

        text << "    add rsp, 16\n";
        return;
    }

    // dynamic array: deep-copy header + heap buffer
    if (type->kind == TypeKind::DynArray) {
        int elemSize = isCompositeMemoryType(type->elementType)
            ? sizeOfType(type->elementType)
            : codegenDynArrayElemSize(type->elementType);
        if (elemSize <= 0) elemSize = 8;

        std::string emptyLabel = newLabel("dyn_empty");
        std::string loopLabel  = newLabel("dyn_copy_loop");
        std::string endLabel   = newLabel("dyn_copy_end");

        // save dst/src
        text << "    push " << dstReg << "\n";
        text << "    push " << srcReg << "\n";

        // len = src[8]
        text << "    mov rax, [" << srcReg << " + 8]\n";
        text << "    mov [" << dstReg << " + 8], rax\n";
        text << "    mov [" << dstReg << " + 16], rax\n";

        text << "    test rax, rax\n";
        text << "    jz " << emptyLabel << "\n";

        // allocate len * elemSize
        text << "    mov rdi, rax\n";
        text << "    imul rdi, " << elemSize << "\n";
        text << "    call lang_alloc\n";

        // restore dst/src addresses without popping
        text << "    mov rbx, [rsp + 8]\n"; // dst
        text << "    mov [rbx], rax\n";     // dst.ptr = new buffer

        text << "    xor rcx, rcx\n";
        text << loopLabel << ":\n";

        text << "    mov rbx, [rsp]\n";       // src header
        text << "    cmp rcx, [rbx + 8]\n";   // rcx < src.len
        text << "    jae " << endLabel << "\n";

        text << "    mov rdi, [rsp + 8]\n";
        text << "    mov rdi, [rdi]\n";
        if (elemSize != 1) {
            text << "    mov rdx, rcx\n";
            text << "    imul rdx, " << elemSize << "\n";
            text << "    add rdi, rdx\n";
        }
        else {
            text << "    add rdi, rcx\n";
        }

        text << "    mov rsi, [rsp]\n";
        text << "    mov rsi, [rsi]\n";
        if (elemSize != 1) {
            text << "    mov rdx, rcx\n";
            text << "    imul rdx, " << elemSize << "\n";
            text << "    add rsi, rdx\n";
        }
        else {
            text << "    add rsi, rcx\n";
        }

        text << "    push rcx\n";
        emitCopy("rdi", "rsi", type->elementType);
        text << "    pop rcx\n";

        text << "    inc rcx\n";
        text << "    jmp " << loopLabel << "\n";

        text << emptyLabel << ":\n";
        text << "    mov rbx, [rsp + 8]\n";
        text << "    mov qword [rbx], 0\n";

        text << endLabel << ":\n";
        text << "    add rsp, 16\n";
        return;
    }

    // struct: value-copy every field
    if (type->kind == TypeKind::Struct) {
        auto orderIt = structFieldOrder.find(type->name);
        auto layoutIt = structLayouts.find(type->name);
        auto typeIt = structFieldTypes.find(type->name);

        if (orderIt != structFieldOrder.end() &&
            layoutIt != structLayouts.end() &&
            typeIt != structFieldTypes.end()) {

            text << "    push " << dstReg << "\n";
            text << "    push " << srcReg << "\n";

            for (const auto& fieldName : orderIt->second) {
                int off = layoutIt->second[fieldName];
                auto fieldType = typeIt->second[fieldName];

                text << "    mov rdi, [rsp + 8]\n";
                if (off != 0) text << "    add rdi, " << off << "\n";

                text << "    mov rsi, [rsp]\n";
                if (off != 0) text << "    add rsi, " << off << "\n";

                emitCopy("rdi", "rsi", fieldType);
            }

            text << "    add rsp, 16\n";
            return;
        }
    }

    // fallback bytewise copy
    int qwords = size / 8;
    int bytes = size % 8;

    for (int i = 0; i < qwords; ++i) {
        int off = i * 8;

        text << "    mov rax, [" << srcReg;
        if (off != 0) text << " + " << off;
        text << "]\n";

        text << "    mov [" << dstReg;
        if (off != 0) text << " + " << off;
        text << "], rax\n";
    }

    for (int i = 0; i < bytes; ++i) {
        int off = qwords * 8 + i;

        text << "    mov al, [" << srcReg << " + " << off << "]\n";
        text << "    mov [" << dstReg << " + " << off << "], al\n";
    }
}

void CodeGen::emitDefaultAt(const std::string& dstReg, int offset, const std::shared_ptr<Type>& type) {
    if (!type) {
        text << "    mov qword [" << dstReg << " + " << offset << "], 0\n";
        return;
    }

    auto addrAt = [&](int off) {
        return "[" + dstReg + " + " + std::to_string(off) + "]";
    };

    switch (type->kind) {
        case TypeKind::Int8:
        case TypeKind::Uint8:
        case TypeKind::Bool:
            text << "    mov byte " << addrAt(offset) << ", 0\n";
            break;

        case TypeKind::Char:
            text << "    mov byte " << addrAt(offset) << ", '0'\n";
            break;

        case TypeKind::Int16:
        case TypeKind::Uint16:
            text << "    mov word " << addrAt(offset) << ", 0\n";
            break;

        case TypeKind::Int32:
        case TypeKind::Uint32:
        case TypeKind::Float32:
            text << "    mov dword " << addrAt(offset) << ", 0\n";
            break;

        case TypeKind::Int64:
        case TypeKind::Uint64:
        case TypeKind::Float64:
        case TypeKind::Class:
        case TypeKind::Null:
            text << "    mov qword " << addrAt(offset) << ", 0\n";
            break;

        case TypeKind::String: {
            std::string label = internString("NULL");
            text << "    lea rax, [rel " << label << "]\n";
            text << "    mov qword " << addrAt(offset) << ", rax\n";
            break;
        }

        case TypeKind::DynArray:
            text << "    mov qword " << addrAt(offset)      << ", 0\n";
            text << "    mov qword " << addrAt(offset + 8)  << ", 0\n";
            text << "    mov qword " << addrAt(offset + 16) << ", 0\n";
            break;

        case TypeKind::Array: {
            int elemSize = storageSizeOfType(type->elementType);
            int n = type->arraySize;

            if (n < 0) n = 0;

            for (int i = 0; i < n; ++i) {
                emitDefaultAt(dstReg, offset + i * elemSize, type->elementType);
            }
            break;
        }
        case TypeKind::Struct: {
            auto orderIt = structFieldOrder.find(type->name);
            auto layoutIt = structLayouts.find(type->name);
            auto typeIt = structFieldTypes.find(type->name);

            if (orderIt == structFieldOrder.end() ||
                layoutIt == structLayouts.end() ||
                typeIt == structFieldTypes.end()) {
                int size = sizeOfType(type);
                for (int i = 0; i < size; ++i) {
                    text << "    mov byte [" << dstReg << " + " << (offset + i) << "], 0\n";
                }
                break;
            }

            for (const auto& fieldName : orderIt->second) {
                int fieldOff = layoutIt->second[fieldName];
                auto fieldType = typeIt->second[fieldName];

                emitDefaultAt(dstReg, offset + fieldOff, fieldType);
            }

            break;
        }

        case TypeKind::Alias:
            text << "    mov qword " << addrAt(offset) << ", 0\n";
            break;


        case TypeKind::Void:
        break;
    }
}

void CodeGen::emitAddress(Expr* expr) {
    if (!expr) return;

    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        if (const LocalVar* lv = findLocal(id->resolvedSym)) {
            text << "    lea rax, [rbp" << lv->offset << "]\n";
            return;
        }

        if (isGlobal(id->resolvedSym)) {
            text << "    lea rax, [rel " << globalLabel(id->resolvedSym, id->name) << "]\n";
            return;
        }

        if (currentClass && hasSelfLocal && (!id->resolvedSym || id->resolvedField)) {
            auto& layout = structLayouts[currentClass->name];
            if (layout.count(id->name)) {
                int off = layout[id->name];

                text << "    mov rax, [rbp" << selfLocal.offset << "]\n";
                emitNullCheck("rax", id->line);
                if (off != 0) {
                    text << "    add rax, " << off << "\n";
                }
                return;
            }
        }

        return;
    }

    if (auto* fa = dynamic_cast<FieldAccess*>(expr)) {
        // StructName.field — default slot структуры.
        if (fa->isTypeDefaultFieldAccess || dynamic_cast<Identifier*>(fa->object)) {
            if (auto* id = dynamic_cast<Identifier*>(fa->object)) {
                if (structDecls.count(id->name)) {
                    text << "    lea rax, [rel __default_" << mangleQualifiedName(id->name) << "_" << fa->field << "]\n";
                    return;
                }
                if (classDecls.count(id->name)) {
                    int off = 0;
                    auto& layout = structLayouts[id->name];
                    if (layout.count(fa->field)) off = layout[fa->field];
                    text << "    lea rax, [rel __default_instance_" << mangleQualifiedName(id->name) << " + " << off << "]\n";
                    return;
                }
            }
            if (auto* na = dynamic_cast<NamespaceAccess*>(fa->object)) {
                std::string typeName = na->nameSpace + "::" + na->member;
                if (structDecls.count(typeName)) {
                    text << "    lea rax, [rel __default_" << mangleQualifiedName(typeName) << "_" << fa->field << "]\n";
                    return;
                }
                if (classDecls.count(typeName)) {
                    int off = 0;
                    auto& layout = structLayouts[typeName];
                    if (layout.count(fa->field)) off = layout[fa->field];
                    text << "    lea rax, [rel __default_instance_" << mangleQualifiedName(typeName) << " + " << off << "]\n";
                    return;
                }
            }
        }

        if (fa->resolvedField) {
            std::shared_ptr<Type> objType = exprType(fa->object);
            if (objType) {
                auto it = structLayouts.find(objType->name);
                if (it != structLayouts.end() && it->second.count(fa->resolvedField->name)) {
                    compileExpr(fa->object);
                    if (objType->kind == TypeKind::Class) {
                        emitNullCheck("rax", fa->line);
                    }
                    int off = it->second[fa->resolvedField->name];
                    if (off != 0) text << "    add rax, " << off << "\n";
                    return;
                }
            }
        }

        if (auto* id = dynamic_cast<Identifier*>(fa->object)) {
            if (structDecls.count(id->name)) {
                text << "    lea rax, [rel __default_" << mangleQualifiedName(id->name) << "_" << fa->field << "]\n";
                return;
            }
        }

        // Для struct expression даёт адрес inline-значения, для class — указатель на heap-объект.
        compileExpr(fa->object);
        
        int off = 0;
        std::shared_ptr<Type> objType = nullptr;
        if (fa->object) objType = exprType(fa->object);

        if (objType) {
            auto it = structLayouts.find(objType->name);
            if (it != structLayouts.end() && it->second.count(fa->field)) {
                off = it->second[fa->field];
            }
        }

        if (objType && objType->kind == TypeKind::Class) {
            emitNullCheck("rax", fa->line);
        }

        if (off != 0) {
            text << "    add rax, " << off << "\n";
        }

        return;
    }

    if (auto* aa = dynamic_cast<ArrayAccess*>(expr)) {
        std::shared_ptr<Type> objType = nullptr;
        if (aa->object) objType = exprType(aa->object);

        if (objType && objType->kind == TypeKind::DynArray) {
            if (!emitDynArrayAddr(aa->object, "rax")) {
                compileExpr(aa->object);
            }

            text << "    push rax\n";
            compileExpr(aa->index);
            text << "    mov rbx, rax\n";
            text << "    pop rax\n";

            std::string okLabel = newLabel("bndok");

            text << "    mov rcx, [rax + 8]\n";
            text << "    cmp rbx, rcx\n";
            text << "    jb " << okLabel << "\n";
            text << "    lea rdi, [rel __rt_bounds]\n";
            text << "    mov rsi, " << aa->line << "\n";
            text << "    call lang_panic\n";
            text << okLabel << ":\n";

            text << "    mov rax, [rax]\n";

            int elemSize = 8;
            if (objType->elementType) {
                elemSize = isCompositeMemoryType(objType->elementType)
                    ? sizeOfType(objType->elementType)
                    : codegenDynArrayElemSize(objType->elementType);
            }

            if (elemSize == 1) {
                text << "    add rax, rbx\n";
            }
            else {
                text << "    imul rbx, " << elemSize << "\n";
                text << "    add rax, rbx\n";
            }

            return;
        }

        if (objType && objType->kind == TypeKind::Array) {
            compileExpr(aa->object);
            text << "    push rax\n";

            compileExpr(aa->index);
            text << "    mov rbx, rax\n";

            text << "    pop rax\n";

            std::string okLabel = newLabel("bndok");

            if (objType->arraySize >= 0) {
                text << "    cmp rbx, " << objType->arraySize << "\n";
                text << "    jb " << okLabel << "\n";
                text << "    lea rdi, [rel __rt_bounds]\n";
                text << "    mov rsi, " << aa->line << "\n";
                text << "    call lang_panic\n";
                text << okLabel << ":\n";
            }

            int elemSize = 8;
            if (objType->elementType) {
                elemSize = storageSizeOfType(objType->elementType);
            }

            if (elemSize == 1) {
                text << "    add rax, rbx\n";
            }
            else {
                text << "    imul rbx, " << elemSize << "\n";
                text << "    add rax, rbx\n";
            }

            return;
        }

        if (objType && objType->kind == TypeKind::String) {
            compileExpr(aa->object);
            text << "    push rax\n";

            compileExpr(aa->index);
            text << "    mov rbx, rax\n";

            text << "    pop rax\n";
            text << "    add rax, rbx\n";
            return;
        }

        // Неизвестная форма массива: используем qword-индексирование.
        compileExpr(aa->object);
        text << "    push rax\n";

        compileExpr(aa->index);
        text << "    mov rbx, rax\n";

        text << "    pop rax\n";
        text << "    lea rax, [rax + rbx*8]\n";
        return;
    }

    if (auto* na = dynamic_cast<NamespaceAccess*>(expr)) {
        if (isGlobal(na->resolvedSym)) {
            text << "    lea rax, [rel " << globalLabel(na->resolvedSym, mangleNamespaceAccess(na)) << "]\n";
        }
        else {
            text << "    lea rax, [rel " << mangleNamespaceAccess(na) << "]\n";
        }
        return;
    }
}

void CodeGen::emitDefault(const std::string& dstReg, const std::shared_ptr<Type>& type) {
    emitDefaultAt(dstReg, 0, type);
}

void CodeGen::compileExprAs(Expr* expr, const std::shared_ptr<Type>& targetType) {
    if (!expr) return;

    compileExpr(expr);

    if (!targetType) return;

    if (isCompositeMemoryType(targetType)) return;

    std::shared_ptr<Type> fromType = exprType(expr);
    emitCastFromTo(fromType, targetType);
}

//  Регистрирует смещение полей struct/class: скаляры/указатели — qword, DynArray (T[]) — 3 qword'а (ptr/len/cap)
void CodeGen::collectLayout(const std::string& name, const std::vector<StructField>& fields) {
    auto& layout = structLayouts[name];
    auto& types = structFieldTypes[name];
    auto& order = structFieldOrder[name];

    layout.clear();
    types.clear();
    order.clear();

    int offset = 0;

    for (auto& field : fields) {
        std::shared_ptr<Type> fieldType = nullptr;

        if (field.resolvedType) {
            fieldType = field.resolvedType;
        }
        else if (field.defaultValue && field.defaultValue->resolvedType) {
            fieldType = field.defaultValue->resolvedType;
        }
        

        if (!fieldType) {
            fieldType = std::make_shared<Type>();
            fieldType->kind = TypeKind::Int64;
        }

        int align = sizeOfType(fieldType);
        if (align <= 0) align = 1;
        if (align > 8) align = 8;

        int rem = offset % align;
        if (rem != 0) {
            offset += align - rem;
        }

        layout[field.name] = offset;
        types[field.name] = fieldType;
        order.push_back(field.name);

        int sz = sizeOfType(fieldType);
        if (sz <= 0) sz = 8;

        offset += sz;
    }

    int rem = offset % 8;
    if (rem != 0) {
        offset += 8 - rem;
    }

    structSizes[name] = offset;
}

//  Обходит AST и регистрирует смещение для всех struct/class/namespace/export
void CodeGen::collectDecls(Stmt* decl) {
    if (!decl) return;
    if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {
        std::string qualifiedName = codegenQualifiedName(currentNamespace, structDecl->name);
        collectLayout(qualifiedName, structDecl->fields);
        if (!structDecls.count(qualifiedName)) {
            structDecls[qualifiedName] = structDecl;
            structDeclNames[structDecl] = qualifiedName;
            structDeclsOrdered.push_back(structDecl);
        }
    }
    else if (auto* classDecl = dynamic_cast<ClassDecl*>(decl)) {
        std::string qualifiedName = codegenQualifiedName(currentNamespace, classDecl->name);
        collectLayout(qualifiedName, classDecl->fields);
        if (!classDecls.count(qualifiedName)) {
            classDecls[qualifiedName] = classDecl;
            classDeclNames[classDecl] = qualifiedName;
            classDeclsOrdered.push_back(classDecl);
        }

        for (auto* nested : classDecl->structs) {
            if (!nested) continue;
            std::string nestedName = qualifiedName + "::" + nested->name;
            collectLayout(nestedName, nested->fields);
            if (!structDecls.count(nestedName)) {
                structDecls[nestedName] = nested;
                structDeclNames[nested] = nestedName;
                structDeclsOrdered.push_back(nested);
            }
        }
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
        collectDecls(exp->decl);
    }
    else if (auto* ns  = dynamic_cast<NamespaceDecl*>(decl)) {
        std::string prevNamespace = currentNamespace;
        currentNamespace = codegenQualifiedName(currentNamespace, ns->name);
        for (auto* stmt : ns->decls) collectDecls(stmt);
        currentNamespace = prevNamespace;
    }
    else if (auto* var = dynamic_cast<VarDecl*>(decl)) {    //  Глобальная переменная: регистрируем имя и её тип
        for (auto* init : var->vars) {
            if (!init) continue;
            if (!init->resolvedSym) continue;
            if (globalsBySymbol.count(init->resolvedSym)) continue;

            std::shared_ptr<Type> type = varInitType(init, var);
            globalsBySymbol[init->resolvedSym] = type;
            globalLabelsBySymbol[init->resolvedSym] = globalLabel(init->resolvedSym, init->name);

            globalVars.push_back({var, init});
        }
    }
}

std::string CodeGen::globalLabel(const std::shared_ptr<Symbol>& sym, const std::string& nameHint) {
    (void)nameHint;

    if (!sym) {
        return "__global_unresolved_0";
    }

    auto it = globalLabelsBySymbol.find(sym);

    if (it != globalLabelsBySymbol.end()) {
        return it->second;
    }

    std::string label = "__global_" + mangleQualifiedName(sym->name) + "_" + ptrSuffix(sym);
    globalLabelsBySymbol[sym] = label;
    return label;
}

bool CodeGen::isGlobal(const std::shared_ptr<Symbol>& sym) const {
    if (!sym) {
        return false;
    }

    return globalsBySymbol.count(sym) > 0;
}

//  Вычисляет адрес DynArray-источника в регистр reg. Возвращает true при успехе.
//  Поддерживает: локальная DynArray; поле текущего класса; ClassName.field (дефолтный экземпляр); глобальная DynArray.
bool CodeGen::emitDynArrayAddr(Expr* e, const char* reg) {
    if (auto* id = dynamic_cast<Identifier*>(e)) {
        const LocalVar* lv = findLocal(id->resolvedSym);
        if (lv) {
            text << "    lea " << reg << ", [rbp" << lv->offset << "]\n";
            return true;
        }
        if (currentClass && hasSelfLocal) {
            auto& layout = structLayouts[currentClass->name];
            if ((!id->resolvedSym || id->resolvedField) && layout.count(id->name)) {
                int off = layout[id->name];
                text << "    mov " << reg << ", [rbp" << selfLocal.offset << "]\n";
                if (off != 0) text << "    add " << reg << ", " << off << "\n";
                return true;
            }
        }
        if (isGlobal(id->resolvedSym)) {
            text << "    lea " << reg << ", [rel " << globalLabel(id->resolvedSym, id->name) << "]\n";
            return true;
        }
    }
    if (auto* fa = dynamic_cast<FieldAccess*>(e)) {
        //  ClassName.field — адрес внутри дефолтного экземпляра
        if (auto* obj = dynamic_cast<Identifier*>(fa->object)) {
            if (classDecls.count(obj->name)) {
                auto& layout = structLayouts[obj->name];
                int off = 0;
                if (layout.count(fa->field)) off = layout[fa->field];
                text << "    lea " << reg << ", [rel __default_instance_" << mangleQualifiedName(obj->name) << " + " << off << "]\n";
                return true;
            }
        }
        //  instance.field — экземпляр (не имя класса): compileExpr кладёт ptr в rax,
        //  затем reg = rax + field_offset.
        if (fa->object && exprType(fa->object)) {
            auto type = exprType(fa->object);
            auto it = structLayouts.find(type->name);
            std::string fieldName = fa->resolvedField ? fa->resolvedField->name : fa->field;
            if (it != structLayouts.end() && it->second.count(fieldName)) {
                int off = it->second[fieldName];
                compileExpr(fa->object);
                if (std::string(reg) == "rax") {
                    if (off != 0) text << "    add rax, " << off << "\n";
                }
                else {
                    text << "    lea " << reg << ", [rax + " << off << "]\n";
                }
                return true;
            }
        }
    }
    return false;
}

void CodeGen::compileMethod(FuncDecl* f, const std::string& labelName) {
    std::string save = f->name;
    f->name = labelName;
    compileFunction(f, true);
    f->name = save;
};

void CodeGen::compileDecl(Stmt* decl, const std::string& classPrefix) {
    if (!decl) return;

    if (auto* func = dynamic_cast<FuncDecl*>(decl)) {   //  Компилируем функцию
        if (func->name == "main") hasMain = true;
        std::string saveName = func->name;

        if (!classPrefix.empty()) {
            func->name = mangleQualifiedName(classPrefix + "::" + func->name);    //  Для функций из namespace
        }

        compileFunction(func);
        func->name = saveName;
        return;
    }

    if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {  //  Рекурсивно компилируем export функции 
        compileDecl(exp->decl, classPrefix); 
        return;
    }

    if (auto* classDecl  = dynamic_cast<ClassDecl*>(decl)) {    //  Компилируем методы класса с self в rdi
        std::string qualifiedClassName = codegenQualifiedName(classPrefix, classDecl->name);
        std::string mangledClassName = mangleQualifiedName(qualifiedClassName);
        std::string saveClassName = classDecl->name;
        classDecl->name = qualifiedClassName;
        currentClass = classDecl;
        if (classDecl->constructor) {
            compileMethod(classDecl->constructor, mangledClassName + "_" + saveClassName);
        }
        if (classDecl->destructor) {
            compileMethod(classDecl->destructor, mangledClassName + "_dtor");
        }
        for (auto* method : classDecl->methods) {
            compileMethod(method, mangledClassName + "_" + method->name);
        }
        currentClass = nullptr;
        classDecl->name = saveClassName;
        return;
    }

    if (auto* ns = dynamic_cast<NamespaceDecl*>(decl)) {
        std::string prefix;
        if (classPrefix.empty()) prefix = ns->name;
        else                     prefix = classPrefix + "::" + ns->name;
        std::string prevNamespace = currentNamespace;
        currentNamespace = prefix;
        for (auto* stmt : ns->decls) compileDecl(stmt, prefix);    //  Функции из namespace помечаем префиксом чтобы различить
        currentNamespace = prevNamespace;
        return;
    }
    //  StructDecl / TypeAlias / ImportDecl / VarDecl на верхнем уровне — ничего не генерируют
    //  (глобалки инициализируются из пролога main через compileGlobalInit).
}

//  Точка входа
std::expected<void, std::string> CodeGen::generate(Program* program, const std::string& outPath) {
    auto isTopLevelMain = [](Stmt* decl) -> bool {
        if (auto* func = dynamic_cast<FuncDecl*>(decl)) {
            return func->name == "main";
        }

        if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
            if (auto* func = dynamic_cast<FuncDecl*>(exp->decl)) {
                return func->name == "main";
            }
        }

        return false;
    };

    text << "extern print_int\n";       //  Импорты рантайма
    text << "extern print_string\n";
    text << "extern print_bool\n";
    text << "extern print_char\n";
    text << "extern print_float\n";
    text << "extern print_space\n";
    text << "extern print_newline\n";
    text << "extern lang_input\n";      //  lang_ префикс — чтобы не конфликтовать с libc (strlen/free/exit/input)
    text << "extern lang_parse_input_int\n";
    text << "extern lang_parse_input_float\n";
    text << "extern lang_parse_input_char\n";
    text << "extern lang_input_array_fixed\n";
    text << "extern lang_input_array_dyn\n";
    text << "extern lang_strlen\n";
    text << "extern lang_panic\n";
    text << "extern lang_exit\n";
    text << "extern lang_alloc\n";
    text << "extern lang_free\n";
    text << "extern lang_push\n";
    text << "extern lang_push_sized\n";
    text << "extern lang_pop\n";
    text << "extern lang_pop_sized\n";
    text << "extern lang_strcat\n";
    text << "extern lang_streq\n\n";

    //  Сообщения рантайма (формирует lang_panic как "runtime error: <msg> at line <N>")
    rodata << "__rt_div_zero: db \"division by zero\", 0\n";
    rodata << "__rt_bounds:   db \"array index out of bounds\", 0\n";
    rodata << "__rt_null_object: db \"null object field access\", 0\n";
    rodata << "__rt_negative_pow: db \"negative exponent\", 0\n";

    //  Первый проход — ставим смещение в struct/class
    for (auto* decl : program->imports) {
        collectDecls(decl);
    }
    for (auto* decl : program->decls) {
        collectDecls(decl);
    }

    //  Выделяем .bss-слоты под default-значения полей структур: один qword на поле с default.
    //  Это позволяет переопределять default в рантайме (`StructName.field = value`).
    for (auto* sd : structDeclsOrdered) {
        std::string structName = structDeclNames.count(sd) ? structDeclNames[sd] : sd->name;
        for (auto& field : sd->fields) {
            if (field.defaultValue) {
                bss << "__default_" << mangleQualifiedName(structName) << "_" << field.name << ": resq 1\n";
            }
        }
    }

    //  Для классов: .bss-слот под «дефолтный экземпляр» (хранит текущее значение полей,
    //  позволяет вызвать MyClass.method() и использовать MyClass.field = x).
    for (auto* cd : classDeclsOrdered) {
        std::string className = classDeclNames.count(cd) ? classDeclNames[cd] : cd->name;
        int size = 0;
        if (structSizes.count(className)) size = structSizes[className];
        if (size <= 0) size = 8;
        bss << "__default_instance_" << mangleQualifiedName(className) << ": resb " << size << "\n";
    }

    //  .bss-слоты под глобальные переменные: DynArray — 24 байта (ptr/len/cap), остальное — 8.
    for (auto& global : globalVars) {
        std::shared_ptr<Type> type = nullptr;
        if (global.var && global.var->resolvedSym) {
            auto it = globalsBySymbol.find(global.var->resolvedSym);
            if (it != globalsBySymbol.end()) {
                type = it->second;
            }
        }
        int size = sizeOfType(type);
        if (size < 8) size = 8;
        bss << globalLabel(global.var->resolvedSym, global.var->name) << ": resb " << size << "\n";
    }

    //  Компилируем каждую функцию верхнего уровня (включая методы классов)
    hasMain = false;
    for (auto* decl : program->imports) {
        if (!isTopLevelMain(decl)) {
            compileDecl(decl, "");
        }
    }
    for (auto* decl : program->decls) {
        compileDecl(decl, "");
    }

    if (!hasMain)
        return std::unexpected("codegen: no 'main' function defined");

    //  Пишем extern для всех использованных C-функций
    for (auto& name : externCFunctions) {
        text << "extern " << name << "\n";
    }

    //  Начало нашего ассемблера
    text << "global _start\n";
    text << "_start:\n";
    text << "    call main\n";
    if (!externCFunctions.empty()) {    //  Если используются C-функции — вызываем libc exit для fflush
        text << "    extern exit\n";
        text << "    mov rdi, rax\n";
        text << "    call exit\n";
    }
    else {
        text << "    mov rdi, rax\n";
        text << "    mov rax, 60\n";
        text << "    syscall\n";
    }

    return finalize(outPath);
}

//  Компиляция функции

void CodeGen::compileFunction(FuncDecl* func, bool isMethod) {
    localsBySymbol.clear();
    hasSelfLocal = false;
    classLocals.clear();
    currentFrameSize = 0;   //  Байт на функцию

    auto previousReturnType = currentReturnType;
    currentReturnType = returnType(func);

    bool previousHasSRet = currentHasSRet;
    int previousSRetOffset = currentSRetOffset;

    currentHasSRet = isCompositeMemoryType(currentReturnType);
    currentSRetOffset = 0;

    std::string symbol = mangleQualifiedName(func->name);
    currentEndLabel = ".end_of_" + symbol;  //  Имя метки переводящей в конец, для return
    text << "global " << symbol << "\n";    //  Определяем нашу функцию глобальной
    text << symbol << ":\n";    //  Метка для неё

    text << "    push rbp\n";   //  Начало функции
    text << "    mov rbp, rsp\n";

    if (currentHasSRet) {
        currentSRetOffset = allocLocal(nullptr, "__sret", nullptr);
    }

    if (isMethod) {     //  Метод: self — неявный параметр после hidden return pointer
        selfLocal = {allocLocal(nullptr, "self", nullptr), nullptr};
        hasSelfLocal = true;
    }

    for (size_t i = 0; i < func->params.size(); i++) {
        auto& param = func->params[i];
        auto type = paramType(func, i);
        allocLocal(param.resolvedSym, param.name, type);
    }

    int paramBytes = currentFrameSize;
    int bodyBytes = countLocalsSize(func->body);
    int frameSize = ((paramBytes + bodyBytes + 15) / 16) * 16;  //  Новый размер фрейма функции

    if (frameSize > 0) {
        text << "    sub rsp, " << frameSize << "\n";   //  Выделяем
    }

    static const char* intArgRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int intIdx = 0, xmmIdx = 0, stackIdx = 0;

    if (currentHasSRet) {
        text << "    mov rax, " << intArgRegs[intIdx++] << "\n";
        emitStore("[rbp" + std::to_string(currentSRetOffset) + "]", nullptr);
    }

    if (isMethod) {
        text << "    mov rax, " << intArgRegs[intIdx++] << "\n";
        emitStore("[rbp" + std::to_string(selfLocal.offset) + "]", nullptr);
    }

    int savedParamRegStart = intIdx;
    int savedParamRegEnd = intIdx;
    {
        int scanIntIdx = intIdx;
        int scanXmmIdx = xmmIdx;
        for (const auto& param : func->params) {
            auto paramType = param.resolvedSym ? param.resolvedSym->type : nullptr;
            bool isFloat = paramType &&
                (paramType->kind == TypeKind::Float32 || paramType->kind == TypeKind::Float64);
            if (isFloat && scanXmmIdx < 8) {
                scanXmmIdx++;
            }
            else if (scanIntIdx < 6) {
                scanIntIdx++;
            }
        }
        savedParamRegEnd = scanIntIdx;
        for (int reg = savedParamRegEnd - 1; reg >= savedParamRegStart; --reg) {
            text << "    push " << intArgRegs[reg] << "\n";
        }
    }

    auto savedIntArg = [&](int regIndex) {
        return "[rsp + " + std::to_string((regIndex - savedParamRegStart) * 8) + "]";
    };

    for (const auto& param : func->params) {
        const LocalVar* localVar = findLocal(param.resolvedSym);
        auto paramType = localVar ? localVar->type : nullptr;

        if (!localVar) {
            continue;
        }

        bool isFloat = paramType &&
            (paramType->kind == TypeKind::Float32 || paramType->kind == TypeKind::Float64);

        // Все составные параметры передаются как адрес source-объекта.
        // Callee сразу делает value-copy в свой локальный слот.
        if (isCompositeMemoryType(paramType)) {
            if (intIdx < 6) {
                text << "    mov rsi, " << savedIntArg(intIdx++) << "\n";
            }
            else {
                int stackOffset = 16 + stackIdx++ * 8;
                text << "    mov rsi, [rbp+" << stackOffset << "]\n";
            }

            text << "    lea rdi, [rbp" << localVar->offset << "]\n";
            emitCopy("rdi", "rsi", paramType);
            continue;
        }

        // float-параметры.
        if (isFloat && xmmIdx < 8) {
            if (paramType->kind == TypeKind::Float32) {
                text << "    movd eax, xmm" << xmmIdx++ << "\n";
            }
            else {
                text << "    movq rax, xmm" << xmmIdx++ << "\n";
            }

            emitStore("[rbp" + std::to_string(localVar->offset) + "]", paramType);
            continue;
        }

        // scalar int/string/bool/char/pointer-подобные параметры в регистрах.
        if (intIdx < 6) {
            text << "    mov rax, " << savedIntArg(intIdx++) << "\n";
            emitStore("[rbp" + std::to_string(localVar->offset) + "]", paramType);
            continue;
        }

        // scalar параметры со стека.
        {
            int stackOffset = 16 + stackIdx++ * 8;
            text << "    mov rax, [rbp+" << stackOffset << "]\n";
            emitStore("[rbp" + std::to_string(localVar->offset) + "]", paramType);
        }
    } 

    if (savedParamRegEnd > savedParamRegStart) {
        text << "    add rsp, " << ((savedParamRegEnd - savedParamRegStart) * 8) << "\n";
    }

    //  В начале main инициализируем default-слоты структур из их объявленных выражений.
    //  Делаем это до тела пользователя, чтобы любые struct-литералы видели уже заполненные значения.
    if (!isMethod && func->name == "main") {
        for (auto* sd : structDeclsOrdered) {
            std::string structName = structDeclNames.count(sd) ? structDeclNames[sd] : sd->name;
            for (auto& field : sd->fields) {
                if (!field.defaultValue) continue;
                compileExpr(field.defaultValue);
                text << "    mov [rel __default_" << mangleQualifiedName(structName) << "_" << field.name << "], rax\n";
            }
        }
        //  И дефолтные экземпляры классов: для каждого поля с default кладём значение
        //  в слот экземпляра по его смещению.
        for (auto* cd : classDeclsOrdered) {
            std::string className = classDeclNames.count(cd) ? classDeclNames[cd] : cd->name;
            auto& layout = structLayouts[className];
            for (auto& field : cd->fields) {
                if (!field.defaultValue) continue;
                int off = 0;
                if (layout.count(field.name)) off = layout[field.name];
                auto fieldType = field.resolvedType;

                if (isCompositeMemoryType(fieldType)) {
                    compileExpr(field.defaultValue);
                    text << "    mov rsi, rax\n";
                    text << "    lea rdi, [rel __default_instance_" << mangleQualifiedName(className) << " + " << off << "]\n";
                    emitCopy("rdi", "rsi", fieldType);
                }
                else {
                    compileExprAs(field.defaultValue, fieldType);
                    emitStore("[rel __default_instance_" + mangleQualifiedName(className) + " + " + std::to_string(off) + "]", fieldType);
                }
            }
        }
        //  Инициализация глобальных переменных — после дефолтов, чтобы init мог ссылаться на StructName/ClassName.
        for (auto& global : globalVars) {
            compileGlobalInit(global.decl, global.var);
        }
    }

    compileStmt(func->body);    //  Тело функции

    text << currentEndLabel << ":\n";   //  Конец функции
    //  Scope-exit: вызываем деструкторы для классовых локалок с ненулевым указателем.
    //  rax — возвращаемое значение, сохраняем его в aligned-слоте, чтобы call'ы не клобберили.
    if (!classLocals.empty()) {
        text << "    sub rsp, 16\n";
        text << "    mov [rsp], rax\n";
        for (auto i = classLocals.rbegin(); i != classLocals.rend(); ++i) {
            std::string skipLabel = newLabel("skip_dtor");
            text << "    mov rdi, [rbp" << i->first << "]\n";
            text << "    test rdi, rdi\n";
            text << "    jz " << skipLabel << "\n";
            text << "    call " << i->second << "_dtor\n";
            text << skipLabel << ":\n";
        }
        text << "    mov rax, [rsp]\n";
        text << "    add rsp, 16\n";
    }
    text << "    mov rsp, rbp\n";
    text << "    pop rbp\n";
    text << "    ret\n";
    text << "\n";
    currentReturnType = previousReturnType;
    currentHasSRet = previousHasSRet;
    currentSRetOffset = previousSRetOffset;
}

int CodeGen::countLocalsSize(Stmt* stmt) {  //  Считаем размер локалок
    if (!stmt) return 0;

    if (auto* block = dynamic_cast<Block*>(stmt)) {     
        int sum = 0;
        for (auto* i : block->statements) {     //  Находим рекурсивно все переменные в блоке
            sum += countLocalsSize(i);
        }
        return sum;
    }
    if (auto* var = dynamic_cast<VarDecl*>(stmt)) { //  Переменные
        int sum = 0;
        for (auto* init : var->vars) {
            std::shared_ptr<Type> type = varInitType(init, var);
            int size = sizeOfType(type);
            if (size < 8) size = 8;     //  Минимум 8 — слоты локалок всегда qword-выровнены
            sum += size;
        }
        return sum;
    }
    if (auto* i = dynamic_cast<If*>(stmt)) {
        return countLocalsSize(i->thenBranch) + countLocalsSize(i->elseBranch);
    }
    if (auto* whileStmt = dynamic_cast<While*>(stmt)) {
        return countLocalsSize(whileStmt->body);
    }
    return 0;
}

void CodeGen::compileStmt(Stmt* stmt) {
    if (!stmt) return;
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto* st : block->statements) {
            compileStmt(st);    //  стэйтменты блока
        }
        return;
    }
    if (auto* var = dynamic_cast<VarDecl*>(stmt)) {
        
        for (auto* init : var->vars) {
            std::shared_ptr<Type> type = varInitType(init, var);

            int off = allocLocal(init->resolvedSym, init->name, type);

            if (type && type->kind == TypeKind::Class && classDecls.count(type->name) && classDecls[type->name]->destructor) {
                classLocals.push_back({off, type->name});
            }
            
            if (init->init) {
                if (type && type->kind == TypeKind::Class) {
                    auto* initCall = dynamic_cast<FuncCall*>(init->init);
                    bool directStore =
                        dynamic_cast<NullLiteral*>(init->init) ||
                        (initCall && initCall->resolvedCallee && initCall->resolvedCallee->kind == SymbolKind::Class);

                    if (directStore) {
                        compileExprAs(init->init, type);
                        emitStore("[rbp" + std::to_string(off) + "]", type);
                    }
                    else {
                        compileExpr(init->init);
                        emitNullCheck("rax", init->init->line);
                        text << "    push rax\n";
                        int size = 8;
                        if (structSizes.count(type->name)) size = structSizes[type->name];
                        if (size <= 0) size = 8;
                        text << "    mov rdi, " << size << "\n";
                        text << "    call lang_alloc\n";
                        text << "    push rax\n";
                        text << "    mov rdi, rax\n";
                        text << "    mov rsi, [rsp + 8]\n";
                        emitCopy("rdi", "rsi", type);
                        text << "    pop rax\n";
                        text << "    add rsp, 8\n";
                        emitStore("[rbp" + std::to_string(off) + "]", type);
                    }
                }
                else if (isCompositeMemoryType(type)) {
                    compileExpr(init->init);
                    text << "    mov rsi, rax\n";
                    text << "    lea rdi, [rbp" << off << "]\n";
                    emitCopy("rdi", "rsi", type);
                }
                else {
                    compileExprAs(init->init, type);
                    emitStore("[rbp" + std::to_string(off) + "]", type);
                }
            }
            else {
                text << "    lea rdi, [rbp" << off << "]\n";
                emitDefault("rdi", type);
            }
        }
        return;
    }
    if (auto* assign = dynamic_cast<Assign*>(stmt)) {
        auto targetType = exprType(assign->target);

        if (assign->op == AssignOp::Assign && targetType && targetType->kind == TypeKind::Class) {
            emitAddress(assign->target);
            text << "    push rax\n";

            auto* valueCall = dynamic_cast<FuncCall*>(assign->value);
            bool directStore =
                dynamic_cast<NullLiteral*>(assign->value) ||
                (valueCall && valueCall->resolvedCallee && valueCall->resolvedCallee->kind == SymbolKind::Class);

            if (directStore) {
                compileExprAs(assign->value, targetType);
            }
            else {
                compileExpr(assign->value);
                emitNullCheck("rax", assign->value->line);
                text << "    push rax\n";
                int size = 8;
                if (structSizes.count(targetType->name)) size = structSizes[targetType->name];
                if (size <= 0) size = 8;
                text << "    mov rdi, " << size << "\n";
                text << "    call lang_alloc\n";
                text << "    push rax\n";
                text << "    mov rdi, rax\n";
                text << "    mov rsi, [rsp + 8]\n";
                emitCopy("rdi", "rsi", targetType);
                text << "    pop rax\n";
                text << "    add rsp, 8\n";
            }

            text << "    pop rbx\n";
            emitStore("[rbx]", targetType);
            return;
        }

        if (assign->op == AssignOp::Assign && isCompositeMemoryType(targetType)) {
            compileExpr(assign->value);
            text << "    push rax\n";
            emitAddress(assign->target);
            text << "    mov rdi, rax\n";
            text << "    pop rsi\n";
            emitCopy("rdi", "rsi", targetType);
            return;
        }

        emitAddress(assign->target);
        text << "    push rax\n";

        if (assign->op == AssignOp::Assign) {
            compileExprAs(assign->value, targetType);
        }
        else {
            text << "    mov rbx, [rsp]\n";
            emitLoad("[rbx]", targetType);
            compileCompoundValue(assign, nullptr);
        }

        text << "    pop rbx\n";
        emitStore("[rbx]", targetType);
        return;
    }
    if (auto* es = dynamic_cast<ExprStmt*>(stmt)) {
        compileExpr(es->expr);
        return;
    }
    if (auto* ret = dynamic_cast<Return*>(stmt)) {
        if (ret->value) {
            if (currentHasSRet && isCompositeMemoryType(currentReturnType)) {
                compileExpr(ret->value);       // rax = src address

                text << "    mov rsi, rax\n";
                text << "    mov rdi, [rbp" << currentSRetOffset << "]\n";
                emitCopy("rdi", "rsi", currentReturnType);

                // Для удобства можно вернуть out_ptr в rax.
                text << "    mov rax, [rbp" << currentSRetOffset << "]\n";
            }
            else {
                compileExprAs(ret->value, currentReturnType);
            }
        }
        text << "    jmp " << currentEndLabel << "\n";
        return;
    }
    if (auto* i = dynamic_cast<If*>(stmt)) {
        std::string elseL = newLabel("else");
        std::string endL  = newLabel("endif");
        compileExpr(i->condition);
        text << "    test rax, rax\n";
        text << "    jz ";
        if (i->elseBranch) text << elseL << "\n";
        else               text << endL  << "\n";
        compileStmt(i->thenBranch);
        if (i->elseBranch) {
            text << "    jmp " << endL << "\n";
            text << elseL << ":\n";
            compileStmt(i->elseBranch);
        }
        text << endL << ":\n";
        return;
    }
    if (auto* whileStmt = dynamic_cast<While*>(stmt)) {
        std::string startL = newLabel("while");
        std::string endL   = newLabel("endwhile");
        loopStack.push_back({endL, startL});
        text << startL << ":\n";
        compileExpr(whileStmt->condition);
        text << "    test rax, rax\n";
        text << "    jz " << endL << "\n";
        compileStmt(whileStmt->body);
        text << "    jmp " << startL << "\n";
        text << endL << ":\n";
        loopStack.pop_back();
        return;
    }
    if (dynamic_cast<Break*>(stmt)) {
        if (!loopStack.empty())
            text << "    jmp " << loopStack.back().breakLabel << "\n";
        return;
    }
    if (dynamic_cast<Continue*>(stmt)) {
        if (!loopStack.empty())
            text << "    jmp " << loopStack.back().continueLabel << "\n";
        return;
    }
}

void CodeGen::compileClassConstruction(const std::string& className, const std::vector<Expr*>& args, const std::shared_ptr<FuncInfo>& ctorInfo) {
    int size = 8;
    if (structSizes.count(className)) {
        size = structSizes[className];
    }
    text << "    mov rdi, " << size << "\n";
    text << "    call lang_alloc\n";
    text << "    push rax\n";                            //  this

    //  Копируем байты дефолтного экземпляра класса в свежевыделенный объект,
    //  чтобы default-значения полей и переопределения (MyClass.field = ...) применились.
    if (classDecls.count(className) && size > 0) {
        int qwords = size / 8;
        int tail   = size % 8;
        for (int k = 0; k < qwords; k++) {
            text << "    mov rax, [rel __default_instance_" << mangleQualifiedName(className) << " + " << (k * 8) << "]\n";
            text << "    mov rbx, [rsp]\n";
            text << "    mov [rbx + " << (k * 8) << "], rax\n";
        }
        for (int k = 0; k < tail; k++) {
            text << "    mov al, [rel __default_instance_" << mangleQualifiedName(className) << " + " << (qwords * 8 + k) << "]\n";
            text << "    mov rbx, [rsp]\n";
            text << "    mov [rbx + " << (qwords * 8 + k) << "], al\n";
        }
    }

    //  Составные поля нельзя шарить bytewise-копией дефолтного экземпляра.
    if (classDecls.count(className)) {
        ClassDecl* cd = classDecls[className];
        auto& layout = structLayouts[cd->name];
        for (auto& field : cd->fields) {
            bool needsValueCopy = field.resolvedType && isCompositeMemoryType(field.resolvedType);
            int off = 0;
            if (layout.count(field.name)) off = layout[field.name];
            if (!needsValueCopy || !field.defaultValue) continue;

            compileExpr(field.defaultValue);
            text << "    mov rsi, rax\n";
            text << "    mov rdi, [rsp]\n";
            if (off != 0) text << "    add rdi, " << off << "\n";
            emitCopy("rdi", "rsi", field.resolvedType);
        }
    }

    bool hasCtor = classDecls.count(className) && classDecls[className]->constructor;
    if (hasCtor) {
        //  Аргументы: пока поддерживаем 0–5 (this занимает rdi)
        for (int i = (int)args.size() - 1; i >= 0; --i) {
            std::shared_ptr<Type> expected = nullptr;
            if (ctorInfo && i < (int)ctorInfo->params.size()) {
                expected = ctorInfo->params[i].type;
            }
            if (expected) compileExprAs(args[i], expected);
            else          compileExpr(args[i]);
            text << "    push rax\n";
        }
        static const char* argRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};
        int intIdx = 0;
        int xmmIdx = 0;

        for (size_t i = 0; i < args.size(); ++i) {
            std::shared_ptr<Type> argType = nullptr;
            if (ctorInfo && i < ctorInfo->params.size()) {
                argType = ctorInfo->params[i].type;
            }

            bool isFloat = argType && (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);
            if (isFloat && xmmIdx < 8) {
                if (argType->kind == TypeKind::Float32) {
                    text << "    movd xmm" << xmmIdx++ << ", dword [rsp]\n";
                }
                else {
                    text << "    movq xmm" << xmmIdx++ << ", [rsp]\n";
                }
                text << "    add rsp, 8\n";
                continue;
            }

            if (intIdx < 5) {
                text << "    pop " << argRegs[intIdx++] << "\n";
            }
        }
        text << "    mov rdi, [rsp]\n";                  //  this снова в rdi
        std::string simpleClassName = className;
        size_t sep = simpleClassName.rfind("::");
        if (sep != std::string::npos) {
            simpleClassName = simpleClassName.substr(sep + 2);
        }
        text << "    call " << mangleQualifiedName(className) << "_" << simpleClassName << "\n";
    }
    text << "    pop rax\n";                             //  результат — указатель на объект
}

void CodeGen::compileCompoundValue(Assign* assign, Expr* currentValue) {
    if (!assign || assign->op == AssignOp::Assign) {
        compileExpr(assign ? assign->value : nullptr);
        return;
    }

    auto targetType = exprType(assign->target);
    auto valueType = exprType(assign->value);

    if (assign->op == AssignOp::AddAssign && targetType && targetType->kind == TypeKind::String) {
        if (currentValue) compileExpr(currentValue);
        text << "    push rax\n";
        compileExpr(assign->value);
        bool rhsIsChar = valueType && valueType->kind == TypeKind::Char;
        if (rhsIsChar) {
            text << "    and rax, 0xFF\n";
        }
        text << "    push rax\n";
        if (rhsIsChar) text << "    mov rsi, rsp\n";
        else           text << "    mov rsi, [rsp]\n";
        text << "    mov rdi, [rsp+8]\n";
        text << "    call lang_strcat\n";
        text << "    add rsp, 16\n";
        return;
    }

    bool isFloat = targetType && (targetType->kind == TypeKind::Float32 || targetType->kind == TypeKind::Float64);
    if (isFloat) {
        if (currentValue) compileExpr(currentValue);
        text << "    push rax\n";
        compileExprAs(assign->value, targetType);

        if (targetType->kind == TypeKind::Float32) {
            text << "    movd xmm1, eax\n";
            text << "    pop rax\n";
            text << "    movd xmm0, eax\n";
            switch (assign->op) {
                case AssignOp::AddAssign: text << "    addss xmm0, xmm1\n"; break;
                case AssignOp::SubAssign: text << "    subss xmm0, xmm1\n"; break;
                case AssignOp::MulAssign: text << "    mulss xmm0, xmm1\n"; break;
                case AssignOp::DivAssign: text << "    divss xmm0, xmm1\n"; break;
                default: break;
            }
            text << "    movd eax, xmm0\n";
        }
        else {
            text << "    movq xmm1, rax\n";
            text << "    pop rax\n";
            text << "    movq xmm0, rax\n";
            switch (assign->op) {
                case AssignOp::AddAssign: text << "    addsd xmm0, xmm1\n"; break;
                case AssignOp::SubAssign: text << "    subsd xmm0, xmm1\n"; break;
                case AssignOp::MulAssign: text << "    mulsd xmm0, xmm1\n"; break;
                case AssignOp::DivAssign: text << "    divsd xmm0, xmm1\n"; break;
                default: break;
            }
            text << "    movq rax, xmm0\n";
        }
        return;
    }

    if (currentValue) compileExpr(currentValue);
    text << "    push rax\n";
    compileExpr(assign->value);
    text << "    mov rbx, rax\n";
    text << "    pop rax\n";

    bool isUnsigned = targetType && (
        targetType->kind == TypeKind::Uint8 || targetType->kind == TypeKind::Uint16 ||
        targetType->kind == TypeKind::Uint32 || targetType->kind == TypeKind::Uint64
    );

    switch (assign->op) {
        case AssignOp::AddAssign:
            text << "    add rax, rbx\n";
            break;
        case AssignOp::SubAssign:
            text << "    sub rax, rbx\n";
            break;
        case AssignOp::MulAssign:
            if (isUnsigned) text << "    mul rbx\n";
            else            text << "    imul rax, rbx\n";
            break;
        case AssignOp::DivAssign:
        case AssignOp::ModAssign: {
            std::string okLabel = newLabel(assign->op == AssignOp::DivAssign ? "divok" : "modok");
            text << "    test rbx, rbx\n";
            text << "    jnz " << okLabel << "\n";
            text << "    lea rdi, [rel __rt_div_zero]\n";
            text << "    mov rsi, " << assign->line << "\n";
            text << "    call lang_panic\n";
            text << okLabel << ":\n";
            if (isUnsigned) text << "    xor rdx, rdx\n    div rbx\n";
            else            text << "    cqo\n    idiv rbx\n";
            if (assign->op == AssignOp::ModAssign) {
                text << "    mov rax, rdx\n";
            }
            break;
        }
        default:
            break;
    }
}

void CodeGen::compileExpr(Expr* expr) {
    if (!expr) return;
    if (auto* num = dynamic_cast<Number*>(expr)) {  //  Встретили число
        //  Тип решаем по resolvedType (семантика расставляет его с учётом контекстной типизации),
        //  а не по синтаксической форме литерала — так `float x = 5` даст 5.0.
        bool asFloat = num->isFloat;
        bool asFloat32 = false;
        if (num->resolvedType) {
            if (num->resolvedType->kind == TypeKind::Float64) asFloat = true;
            else if (num->resolvedType->kind == TypeKind::Float32) { asFloat = true; asFloat32 = true; }
        }
        if (asFloat) {
            if (asFloat32) {
                float value = (float)num->value;
                uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                text << "    mov rax, " << bits << "\n";
            } else {
                double value = num->value;
                uint64_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                text << "    mov rax, " << bits << "\n";
            }
            return;
        }
        text << "    mov rax, " << (long long)num->value << "\n";   //  Обычное число просто закидываем
        return;
    }
    if (auto* boolLit = dynamic_cast<Bool*>(expr)) {  //  Булев тип
        if (boolLit->value) {
            text << "    mov rax, 1\n";
        }
        else {
            text << "    mov rax, 0\n";
        }
        return;
    }
    if (auto* charLit = dynamic_cast<CharLiteral*>(expr)) {
        text << "    mov rax, " << (int)(unsigned char)charLit->value << "\n";
        return;
    }
    if (dynamic_cast<NullLiteral*>(expr)) {
        text << "    xor rax, rax\n";
        return;
    }
    if (auto* str = dynamic_cast<String*>(expr)) {  //  Строка
        //  Если семантик сузил литерал до Char ("!" в `char c = "!";`) — загружаем сам байт, а не адрес.
        auto type = exprType(expr);
        if (type && type->kind == TypeKind::Char && str->value.size() == 1) {
            text << "    mov rax, " << (int)(unsigned char)str->value[0] << "\n";
            return;
        }
        std::string label = internString(str->value);   //  пишем строку в rodata
        text << "    lea rax, [rel " << label << "]\n"; //  берём адрес строки по её метке
        return;
    }
   
    if (auto* arrayLit = dynamic_cast<ArrayLiteral*>(expr)) {
        auto arrType = exprType(expr);

        if (!arrType) {
            text << "    xor rax, rax\n";
            return;
        }

        // dynamic array literal: создаём header {ptr,len,cap} + heap-buffer
        if (arrType->kind == TypeKind::DynArray) {
            int n = (int)arrayLit->elements.size();
            auto elemType = arrType->elementType;
            int elemSize = isCompositeMemoryType(elemType)
                ? sizeOfType(elemType)
                : codegenDynArrayElemSize(elemType);
            if (elemSize <= 0) elemSize = 8;

            text << "    mov rdi, 24\n";
            text << "    call lang_alloc\n";
            text << "    push rax\n"; // header

            if (n == 0) {
                text << "    mov rbx, [rsp]\n";
                text << "    mov qword [rbx], 0\n";
                text << "    mov qword [rbx + 8], 0\n";
                text << "    mov qword [rbx + 16], 0\n";
                text << "    pop rax\n";
                return;
            }

            text << "    mov rdi, " << (n * elemSize) << "\n";
            text << "    call lang_alloc\n";

            text << "    mov rbx, [rsp]\n";
            text << "    mov [rbx], rax\n";
            text << "    mov qword [rbx + 8], " << n << "\n";
            text << "    mov qword [rbx + 16], " << n << "\n";

            for (int i = 0; i < n; ++i) {
                int off = i * elemSize;

                if (isCompositeMemoryType(elemType)) {
                    compileExpr(arrayLit->elements[i]);
                    text << "    mov rsi, rax\n";
                    text << "    mov rdi, [rsp]\n";
                    text << "    mov rdi, [rdi]\n";
                    if (off != 0) text << "    add rdi, " << off << "\n";
                    emitCopy("rdi", "rsi", elemType);
                }
                else {
                    compileExprAs(arrayLit->elements[i], elemType);
                    text << "    mov rbx, [rsp]\n";
                    text << "    mov rbx, [rbx]\n";
                    emitStore("[rbx + " + std::to_string(off) + "]", elemType);
                }
            }

            text << "    pop rax\n";
            return;
        }

        // fixed array literal: создаём inline temporary
        if (arrType->kind == TypeKind::Array) {
            int size = sizeOfType(arrType);
            int elemSize = sizeOfType(arrType->elementType);
            if (size <= 0) size = 8;
            if (elemSize <= 0) elemSize = 8;

            text << "    mov rdi, " << size << "\n";
            text << "    call lang_alloc\n";
            text << "    push rax\n";

            text << "    mov rdi, [rsp]\n";
            emitDefault("rdi", arrType);

            for (size_t i = 0; i < arrayLit->elements.size(); ++i) {
                int off = (int)i * elemSize;

                if (isCompositeMemoryType(arrType->elementType)) {
                    compileExpr(arrayLit->elements[i]);
                    text << "    mov rsi, rax\n";
                    text << "    mov rdi, [rsp]\n";
                    if (off != 0) text << "    add rdi, " << off << "\n";
                    emitCopy("rdi", "rsi", arrType->elementType);
                }
                else {
                    compileExprAs(arrayLit->elements[i], arrType->elementType);
                    text << "    mov rbx, [rsp]\n";
                    emitStore("[rbx + " + std::to_string(off) + "]", arrType->elementType);
                }
            }

            text << "    pop rax\n";
            return;
        }

        text << "    xor rax, rax\n";
        return;
    }
   
    if (auto* sl = dynamic_cast<StructLiteral*>(expr)) {
        auto structType = exprType(expr);
        int size = sizeOfType(structType);
        if (size <= 0) size = 8;

        text << "    mov rdi, " << size << "\n";
        text << "    call lang_alloc\n";
        text << "    push rax\n";

        // default всего объекта
        text << "    mov rdi, [rsp]\n";
        emitDefault("rdi", structType);

        std::string structName = structType ? structType->name : sl->name;
        auto& layout = structLayouts[structName];
        auto& types = structFieldTypes[structName];

        for (auto& fi : sl->fields) {
            int off = 0;
            if (layout.count(fi.name)) off = layout[fi.name];

            std::shared_ptr<Type> fieldType = nullptr;
            if (types.count(fi.name)) fieldType = types[fi.name];

            if (isCompositeMemoryType(fieldType)) {
                compileExpr(fi.value);          // rax = src address
                text << "    mov rsi, rax\n";
                text << "    mov rdi, [rsp]\n";
                if (off != 0) text << "    add rdi, " << off << "\n";
                emitCopy("rdi", "rsi", fieldType);
            }
            else {
                compileExprAs(fi.value, fieldType);
                text << "    mov rbx, [rsp]\n";
                emitStore("[rbx + " + std::to_string(off) + "]", fieldType);
            }
        }

        text << "    pop rax\n";
        return;
    }
    
    if (auto* fa = dynamic_cast<FieldAccess*>(expr)) {
        emitAddress(fa);
        auto type = fieldType(fa);
        if (!isCompositeMemoryType(type)) {
            emitLoad("[rax]", type);
        }
        return;
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(expr)) {
        emitAddress(aa);
        auto type = exprType(aa);
        if (!isCompositeMemoryType(type)) {
            emitLoad("[rax]", type);
        }
        return;
    }
    if (auto* castExpr = dynamic_cast<CastExpr*>(expr)) {
       auto targetType = castExpr->resolvedType;

        if (!targetType) {
            // После успешной семантики такого быть не должно.
            compileExpr(castExpr->value);
            return;
        }

        compileExprAs(castExpr->value, targetType);
        return; 
    }
    if (auto* na = dynamic_cast<NamespaceAccess*>(expr)) {
        auto type = exprType(na);
        emitAddress(na);
        if (!isCompositeMemoryType(type)) {
            emitLoad("[rax]", type);
        }
        return;
    }
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        const LocalVar* lv = findLocal(id->resolvedSym);
        if (lv) {
            emitAddress(id);
            if (!isCompositeMemoryType(lv->type)) {
                emitLoad("[rax]", lv->type);
            }
            return;
        }
        if (isGlobal(id->resolvedSym)) {
            auto globalType = id->resolvedSym && id->resolvedSym->type ? id->resolvedSym->type : exprType(id);
            if (id->resolvedSym) {
                auto it = globalsBySymbol.find(id->resolvedSym);
                if (it != globalsBySymbol.end()) {
                    globalType = it->second;
                }
            }
            emitAddress(id);
            if (!isCompositeMemoryType(globalType)) {
                emitLoad("[rax]", globalType);
            }
            return;
        }
        //  Сокращение: бареИмяСтруктуры = пустой struct-литерал `Struct {}`.
        //  Аллоцируем объект и заливаем default-значения из слотов (поля без default остаются нулями от lang_alloc).
        if ((!id->resolvedSym || id->resolvedSym->kind == SymbolKind::Struct) && structDecls.count(id->name)) {
            int size;
            if (structSizes.count(id->name)) size = structSizes[id->name];
            else                              size = (int)structDecls[id->name]->fields.size() * 8;
            text << "    mov rdi, " << size << "\n";
            text << "    call lang_alloc\n";
            text << "    push rax\n";
            auto& layout = structLayouts[id->name];
            for (auto& field : structDecls[id->name]->fields) {
                if (!field.defaultValue) continue;
                text << "    mov rax, [rel __default_" << mangleQualifiedName(id->name) << "_" << field.name << "]\n";
                text << "    mov rbx, [rsp]\n";
                int off = 0;
                if (layout.count(field.name)) off = layout[field.name];
                text << "    mov [rbx+" << off << "], rax\n";
            }
            text << "    pop rax\n";
            return;
        }
        //  Имя класса — возвращаем адрес дефолтного экземпляра (для MyClass.method()/MyClass.field).
        if ((!id->resolvedSym || id->resolvedSym->kind == SymbolKind::Class) && classDecls.count(id->name)) {
            text << "    lea rax, [rel __default_instance_" << mangleQualifiedName(id->name) << "]\n";
            return;
        }
        //  Внутри метода имя может быть полем текущего класса — читаем через self (rbp+self_off) + field_off.
        if (currentClass && (!id->resolvedSym || id->resolvedField)) {
            auto& layout = structLayouts[currentClass->name];
            if (layout.count(id->name)) {
                emitAddress(id);
                auto type = exprType(id);
                if (!isCompositeMemoryType(type)) {
                    emitLoad("[rax]", type);
                }
                return;
            }
        }
        return;                                      //  Семантик уже проверил существование
    }
    if (auto* unary = dynamic_cast<Unary*>(expr)) {
        if (unary->op == Operand::UnaryPlus) {
            compileExpr(unary->operand);
            return;
        }
        if (unary->op == Operand::UnaryMinus) {
            compileExpr(unary->operand);
            std::shared_ptr<Type> t = nullptr;
            if (unary->operand) t = exprType(unary->operand);
            bool isFloat = t && (t->kind == TypeKind::Float32 || t->kind == TypeKind::Float64);
            if (isFloat) {
                //  Для float — инвертируем только знаковый бит, не все биты
                if (t->kind == TypeKind::Float32) {
                    text << "    mov ebx, 0x80000000\n";
                }
                else {
                    text << "    mov rbx, 0x8000000000000000\n";
                }
                text << "    xor rax, rbx\n";
            }
            else {
                text << "    neg rax\n";
            }
            return;
        }
        if (unary->op == Operand::Increment || unary->op == Operand::Decrement) {
            //  ++/-- работают с любым scalar lvalue: локалкой, глобалкой,
            //  полем или элементом массива. Адрес вычисляется один раз, чтобы
            //  индексные выражения с побочными эффектами не выполнялись дважды.
            auto type = exprType(unary->operand);
            emitAddress(unary->operand);
            text << "    push rax\n";
            emitLoad("[rax]", type);
            if (unary->op == Operand::Increment) {
                text << "    add rax, 1\n";
            }
            else {
                text << "    sub rax, 1\n";
            }
            text << "    mov rbx, [rsp]\n";
            emitStore("[rbx]", type);
            text << "    pop rbx\n";
            return;
        }
        if (unary->op == Operand::Not) {
            compileExpr(unary->operand);
            text << "    xor rax, 1\n";
            return;
        }
        return;
    }
    if (auto* bin = dynamic_cast<Binary*>(expr)) {
        //  Short-circuit для && и || — нельзя вычислять оба операнда заранее
        if (bin->op == Operand::And || bin->op == Operand::Or) {
            bool isAnd = (bin->op == Operand::And);
            std::string shortL;
            if (isAnd) {
                shortL = newLabel("and_false");
            }
            else {
                shortL = newLabel("or_true");
            }
            std::string endL = newLabel("logic_end");
            const char* jmpCC;
            if (isAnd) {
                jmpCC = "jz";
            }
            else {
                jmpCC = "jnz";
            }

            compileExpr(bin->left);
            text << "    test rax, rax\n";
            text << "    " << jmpCC << " " << shortL << "\n";
            compileExpr(bin->right);
            text << "    test rax, rax\n";
            text << "    " << jmpCC << " " << shortL << "\n";
            if (isAnd) {
                text << "    mov rax, 1\n";
            }
            else {
                text << "    xor rax, rax\n";
            }
            text << "    jmp " << endL << "\n";
            text << shortL << ":\n";
            if (isAnd) {
                text << "    xor rax, rax\n";
            }
            else {
                text << "    mov rax, 1\n";
            }
            text << endL << ":\n";
            return;
        }

        //  Конкатенация строк: left + right → lang_strcat(left, right).
        //  Операнд-char превращаем в указатель на временную 2-байтовую строку "{c,\0}" на стеке.
        auto leftType = exprType(bin->left);
        auto rightType = exprType(bin->right);
        bool leftIsStr  = leftType  && leftType->kind  == TypeKind::String;
        bool rightIsStr = rightType && rightType->kind == TypeKind::String;
        bool leftIsChar  = leftType  && leftType->kind  == TypeKind::Char;
        bool rightIsChar = rightType && rightType->kind == TypeKind::Char;
        bool isStringConcat = bin->op == Operand::Add
            && (leftIsStr  || leftIsChar)
            && (rightIsStr || rightIsChar)
            && (leftIsStr  || rightIsStr);
        if (isStringConcat) {
            //  Для каждого операнда-char пушим qword с младшим байтом = символ, остальные = 0.
            //  Получаем валидный C-string из 2 байт: {c, '\0'} лежит по адресу rsp на момент вызова.
            //  Для string просто pushим указатель.
            compileExpr(bin->left);
            if (leftIsChar) {
                text << "    and rax, 0xFF\n";                   //  обнулили верхние байты, в низком — символ
            }
            text << "    push rax\n";                            //  [rsp] = left (либо ptr, либо qword-"{c,\\0,...}")
            compileExpr(bin->right);
            if (rightIsChar) {
                text << "    and rax, 0xFF\n";
            }
            text << "    push rax\n";                            //  [rsp] = right
            if (rightIsChar) text << "    mov rsi, rsp\n";       //  указатель на наш qword как C-строку
            else             text << "    mov rsi, [rsp]\n";     //  указатель-строка
            if (leftIsChar)  text << "    lea rdi, [rsp+8]\n";   //  адрес qword'а с символом на стеке
            else             text << "    mov rdi, [rsp+8]\n";
            text << "    call lang_strcat\n";
            text << "    add rsp, 16\n";
            return;
        }

        if ((bin->op == Operand::EqualEqual || bin->op == Operand::NotEqual) && leftIsStr && rightIsStr) {
            compileExpr(bin->left);
            text << "    push rax\n";
            compileExpr(bin->right);
            text << "    mov rsi, rax\n";
            text << "    pop rdi\n";
            text << "    call lang_streq\n";
            if (bin->op == Operand::NotEqual) {
                text << "    xor rax, 1\n";
            }
            return;
        }

        //  Float-арифметика/сравнения: оба операнда приводим к общему float-типу.
        auto binaryResultType = exprType(bin);
        auto numericType = isCodegenFloatType(binaryResultType)
            ? binaryResultType
            : codegenCommonNumericType(leftType, rightType);
        bool isFloat = isCodegenFloatType(numericType);
        if (isFloat && bin->op != Operand::Mod) {
            compileExprAs(bin->left, numericType);
            text << "    push rax\n";
            compileExprAs(bin->right, numericType);

            if (numericType->kind == TypeKind::Float32) {
                text << "    movd xmm1, eax\n";
                text << "    pop rax\n";
                text << "    movd xmm0, eax\n";
            }
            else {
                text << "    movq xmm1, rax\n";
                text << "    pop rax\n";
                text << "    movq xmm0, rax\n";
            }

            auto emitFloatCompare = [&](const char* cmpInstr) {
                text << "    " << cmpInstr << " xmm0, xmm1\n";
                const char* cc = "e";
                bool notEqual = false;

                switch (bin->op) {
                    case Operand::Less: cc = "b";  break;
                    case Operand::Greater: cc = "a";  break;
                    case Operand::LessEqual: cc = "be"; break;
                    case Operand::GreaterEqual: cc = "ae"; break;
                    case Operand::EqualEqual: cc = "e";  break;
                    case Operand::NotEqual:
                        cc = "ne";
                        notEqual = true;
                        break;
                    default: break;
                }

                text << "    set" << cc << " al\n";
                if (notEqual) {
                    text << "    setp dl\n";
                    text << "    or al, dl\n";
                }
                else {
                    text << "    setnp dl\n";
                    text << "    and al, dl\n";
                }
                text << "    movzx rax, al\n";
            };

            auto emitFloatPow = [&](bool isFloat32) {
                if (isFloat32) {
                    text << "    sub rsp, 8\n";
                    text << "    movd dword [rsp], xmm0\n";
                    text << "    movd dword [rsp + 4], xmm1\n";
                    text << "    fld dword [rsp + 4]\n";
                    text << "    fld dword [rsp]\n";
                }
                else {
                    text << "    sub rsp, 16\n";
                    text << "    movq qword [rsp], xmm0\n";
                    text << "    movq qword [rsp + 8], xmm1\n";
                    text << "    fld qword [rsp + 8]\n";
                    text << "    fld qword [rsp]\n";
                }

                text << "    fyl2x\n";
                text << "    fld st0\n";
                text << "    frndint\n";
                text << "    fsub st1, st0\n";
                text << "    fxch st1\n";
                text << "    f2xm1\n";
                text << "    fld1\n";
                text << "    faddp st1, st0\n";
                text << "    fscale\n";
                text << "    fstp st1\n";

                if (isFloat32) {
                    text << "    fstp dword [rsp]\n";
                    text << "    movd xmm0, dword [rsp]\n";
                    text << "    add rsp, 8\n";
                    text << "    movd eax, xmm0\n";
                }
                else {
                    text << "    fstp qword [rsp]\n";
                    text << "    movq xmm0, qword [rsp]\n";
                    text << "    add rsp, 16\n";
                    text << "    movq rax, xmm0\n";
                }
            };

            if (numericType->kind == TypeKind::Float32) {
                switch (bin->op) {
                    case Operand::Add: text << "    addss xmm0, xmm1\n"; break;
                    case Operand::Sub: text << "    subss xmm0, xmm1\n"; break;
                    case Operand::Mul: text << "    mulss xmm0, xmm1\n"; break;
                    case Operand::Div: text << "    divss xmm0, xmm1\n"; break;
                    case Operand::Pow:
                        emitFloatPow(true);
                        return;
                    case Operand::Less:
                    case Operand::Greater:
                    case Operand::LessEqual:
                    case Operand::GreaterEqual:
                    case Operand::EqualEqual:
                    case Operand::NotEqual:
                        emitFloatCompare("ucomiss");
                        return;
                    default: break;
                }
                text << "    movd eax, xmm0\n";
            }
            else {
                switch (bin->op) {
                    case Operand::Add: text << "    addsd xmm0, xmm1\n"; break;
                    case Operand::Sub: text << "    subsd xmm0, xmm1\n"; break;
                    case Operand::Mul: text << "    mulsd xmm0, xmm1\n"; break;
                    case Operand::Div: text << "    divsd xmm0, xmm1\n"; break;
                    case Operand::Pow:
                        emitFloatPow(false);
                        return;
                    case Operand::Less:
                    case Operand::Greater:
                    case Operand::LessEqual:
                    case Operand::GreaterEqual:
                    case Operand::EqualEqual:
                    case Operand::NotEqual:
                        emitFloatCompare("ucomisd");
                        return;
                    default: break;
                }
                text << "    movq rax, xmm0\n";
            }

            if (bin->op == Operand::Add || bin->op == Operand::Sub ||
                bin->op == Operand::Mul || bin->op == Operand::Div) {
                return;
            }

            //  Неподдержанная float-операция должна была быть отфильтрована семантикой.
            return;
        }

        //  Вычисляем операнды: левый → на стек, правый → rbx, левый → rax
        compileExpr(bin->left);
        text << "    push rax\n";
        compileExpr(bin->right);
        text << "    mov rbx, rax\n";
        text << "    pop rax\n";

        //  Знаковость — по типу операнда (для сравнений bin->resolvedType всегда bool)
        bool isUnsigned = false;
        if (numericType) {
            auto kind = numericType->kind;
            isUnsigned = (kind == TypeKind::Uint8  || kind == TypeKind::Uint16 || kind == TypeKind::Uint32 || kind == TypeKind::Uint64);
        }

        switch (bin->op) {
            case Operand::Add: text << "    add rax, rbx\n"; break;
            case Operand::Sub: text << "    sub rax, rbx\n"; break;
            case Operand::Mul:
                if (isUnsigned) text << "    mul rbx\n";           //  rdx:rax = rax * rbx
                else            text << "    imul rax, rbx\n";
                break;
            case Operand::Pow: {
                std::string nonNegativeLabel = newLabel("pow_nonneg");
                std::string loopLabel = newLabel("pow_loop");
                std::string doneLabel = newLabel("pow_done");

                text << "    test rbx, rbx\n";
                text << "    jns " << nonNegativeLabel << "\n";
                text << "    lea rdi, [rel __rt_negative_pow]\n";
                text << "    mov rsi, " << bin->line << "\n";
                text << "    call lang_panic\n";
                text << nonNegativeLabel << ":\n";
                text << "    mov rcx, rbx\n";
                text << "    mov rbx, rax\n";
                text << "    mov rax, 1\n";
                text << loopLabel << ":\n";
                text << "    test rcx, rcx\n";
                text << "    jz " << doneLabel << "\n";
                text << "    imul rax, rbx\n";
                text << "    dec rcx\n";
                text << "    jmp " << loopLabel << "\n";
                text << doneLabel << ":\n";
                break;
            }
            case Operand::Div: {
                std::string okLabel = newLabel("divok");
                text << "    test rbx, rbx\n";
                text << "    jnz " << okLabel << "\n";
                text << "    lea rdi, [rel __rt_div_zero]\n";
                text << "    mov rsi, " << bin->line << "\n";
                text << "    call lang_panic\n";
                text << okLabel << ":\n";
                if (isUnsigned) text << "    xor rdx, rdx\n    div rbx\n";
                else            text << "    cqo\n    idiv rbx\n";
                break;
            }
            case Operand::Mod: {
                std::string okLabel = newLabel("modok");
                text << "    test rbx, rbx\n";
                text << "    jnz " << okLabel << "\n";
                text << "    lea rdi, [rel __rt_div_zero]\n";
                text << "    mov rsi, " << bin->line << "\n";
                text << "    call lang_panic\n";
                text << okLabel << ":\n";
                if (isUnsigned) text << "    xor rdx, rdx\n    div rbx\n";
                else            text << "    cqo\n    idiv rbx\n";
                text << "    mov rax, rdx\n";
                break;
            }
            case Operand::Less: 
            case Operand::Greater:
            case Operand::LessEqual:
            case Operand::GreaterEqual:
            case Operand::EqualEqual: 
            case Operand::NotEqual: {
                text << "    cmp rax, rbx\n";
                const char* cc = "e";
                switch (bin->op) {
                    case Operand::Less:
                        if (isUnsigned) cc = "b";
                        else            cc = "l";
                        break;
                    case Operand::Greater:
                        if (isUnsigned) cc = "a";
                        else            cc = "g";
                        break;
                    case Operand::LessEqual:
                        if (isUnsigned) cc = "be";
                        else            cc = "le";
                        break;
                    case Operand::GreaterEqual:
                        if (isUnsigned) cc = "ae";
                        else            cc = "ge";
                        break;
                    case Operand::EqualEqual:   cc = "e";  break;
                    case Operand::NotEqual:     cc = "ne"; break;
                    default: break;
                }
                text << "    set" << cc << " al\n";
                text << "    movzx rax, al\n";
                break;
            }
            default: break;                                //  And/Or обработаны выше
        }
        return;
    }
    if (auto* fc = dynamic_cast<FuncCall*>(expr)) {
        auto funcInfo = callFuncInfo(fc);
        //  Метод экземпляра: obj.method(args) — self передаётся в rdi
       
        if (auto* fa = dynamic_cast<FieldAccess*>(fc->callee)) {
            std::shared_ptr<Type> objType;
            if (fa->object) {
                objType = exprType(fa->object);
            }

            if (!objType || objType->kind != TypeKind::Class) {
                return;
            }

            auto retType = fc->resolvedType ? fc->resolvedType : callReturnType(fc);
            bool returnsComposite = isCompositeMemoryType(retType);

            compileExpr(fa->object);
            emitNullCheck("rax", fa->line);
            text << "    push rax\n"; // self

            for (int i = (int)fc->args.size() - 1; i >= 0; --i) {
                std::shared_ptr<Type> expected = nullptr;

                if (fc->resolvedMethod && i < (int)fc->resolvedMethod->params.size()) {
                    expected = fc->resolvedMethod->params[i].type;
                }

                if (expected) {
                    compileExprAs(fc->args[i], expected);
                }
                else {
                    compileExpr(fc->args[i]);
                }

                // scalar -> value, composite -> address
                text << "    push rax\n";
            }

            if (returnsComposite) {
                int retSize = sizeOfType(retType);
                if (retSize <= 0) retSize = 8;
                text << "    mov rdi, " << retSize << "\n";
                text << "    call lang_alloc\n";
                text << "    push rax\n";
            }

            static const char* methodArgRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};
            static const char* sretMethodArgRegs[4] = {"rdx", "rcx", "r8", "r9"};

            if (returnsComposite) {
                text << "    pop rdi\n"; // hidden return pointer
                text << "    mov rsi, [rsp + " << (fc->args.size() * 8) << "]\n"; // self
            }
            else {
                text << "    mov rdi, [rsp + " << (fc->args.size() * 8) << "]\n"; // self
            }

            int intIdx = 0;
            int xmmIdx = 0;
            size_t qwordsConsumed = 0;
            size_t maxIntRegs = returnsComposite ? 4 : 5;

            for (size_t i = 0; i < fc->args.size(); ++i) {
                std::shared_ptr<Type> argType = nullptr;
                if (fc->resolvedMethod && i < fc->resolvedMethod->params.size()) {
                    argType = fc->resolvedMethod->params[i].type;
                }
                else {
                    argType = exprType(fc->args[i]);
                }

                bool isFloat = argType && (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);
                if (isFloat && xmmIdx < 8) {
                    if (argType->kind == TypeKind::Float32) {
                        text << "    movd xmm" << xmmIdx++ << ", dword [rsp]\n";
                    }
                    else {
                        text << "    movq xmm" << xmmIdx++ << ", [rsp]\n";
                    }
                    text << "    add rsp, 8\n";
                    qwordsConsumed++;
                    continue;
                }

                if ((size_t)intIdx < maxIntRegs) {
                    if (returnsComposite) {
                        text << "    pop " << sretMethodArgRegs[intIdx++] << "\n";
                    }
                    else {
                        text << "    pop " << methodArgRegs[intIdx++] << "\n";
                    }
                    qwordsConsumed++;
                }
            }

            size_t stackArgs = fc->args.size() - qwordsConsumed;
            if (stackArgs == 0) {
                text << "    add rsp, 8\n"; // self
            }

            text << "    call " << mangleQualifiedName(objType->name) << "_" << fa->field << "\n";

            if (stackArgs > 0) {
                text << "    add rsp, " << (stackArgs * 8 + 8) << "\n";
            }

            return;
        }

        std::string callName;
        std::string callLabel;
        if (auto* id = dynamic_cast<Identifier*>(fc->callee)) {
            callName = id->name;
            callLabel = symbolLabel(fc->resolvedCallee, callName);
        }
        else if (auto* na = dynamic_cast<NamespaceAccess*>(fc->callee)) {
            callName = na->member;
            callLabel = mangleNamespaceAccess(na);
        }
        else return;

        //  Создание экземпляра класса по текущей грамматике: ClassName(args...).
        if (fc->resolvedCallee && fc->resolvedCallee->kind == SymbolKind::Class) {
            auto classType = fc->resolvedCallee->type ? fc->resolvedCallee->type : fc->resolvedType;
            if (classType && classType->kind == TypeKind::Class) {
                auto ctorInfo = fc->resolvedCallee->classInfo ? fc->resolvedCallee->classInfo->constructor : nullptr;
                compileClassConstruction(classType->name, fc->args, ctorInfo);
                return;
            }
        }
        if (auto* id = dynamic_cast<Identifier*>(fc->callee)) {
            auto returnType = fc->resolvedType ? fc->resolvedType : callReturnType(fc);
            if (classDecls.count(id->name) && returnType && returnType->kind == TypeKind::Class) {
                compileClassConstruction(id->name, fc->args, funcInfo);
                return;
            }
        }

        //  Встроенные функции
        //  input() — контекстный auto-возврат. Базовый рантайм читает строку,
        //  а типизированные хелперы парсят её под ожидаемый тип.
        if (callName == "input" && fc->args.empty()) {
            auto inputType = fc->resolvedType ? fc->resolvedType : callReturnType(fc);

            if (inputType && inputType->kind == TypeKind::String) {
                text << "    call lang_input\n";
                return;
            }

            if (inputType && inputType->kind == TypeKind::Char) {
                text << "    call lang_input\n";
                text << "    mov rdi, rax\n";
                text << "    mov rsi, " << fc->line << "\n";
                text << "    call lang_parse_input_char\n";
                return;
            }

            if (isCodegenIntType(inputType)) {
                text << "    call lang_input\n";
                text << "    mov rdi, rax\n";
                text << "    mov rsi, " << fc->line << "\n";
                text << "    call lang_parse_input_int\n";
                return;
            }

            if (isCodegenFloatType(inputType)) {
                text << "    call lang_input\n";
                text << "    mov rdi, rax\n";
                text << "    mov rsi, " << fc->line << "\n";
                text << "    call lang_parse_input_float\n";
                if (inputType->kind == TypeKind::Float32) {
                    text << "    movq xmm0, rax\n";
                    text << "    cvtsd2ss xmm0, xmm0\n";
                    text << "    movd eax, xmm0\n";
                }
                return;
            }

            if (isCodegenInputArrayType(inputType) && inputType->kind == TypeKind::Array) {
                int typeCode = codegenInputTypeCode(inputType->elementType);
                text << "    mov rdi, " << typeCode << "\n";
                text << "    mov rsi, " << inputType->arraySize << "\n";
                text << "    mov rdx, " << fc->line << "\n";
                text << "    call lang_input_array_fixed\n";
                return;
            }

            if (isCodegenInputArrayType(inputType) && inputType->kind == TypeKind::DynArray) {
                int typeCode = codegenInputTypeCode(inputType->elementType);
                text << "    mov rdi, " << typeCode << "\n";
                text << "    mov rsi, " << fc->line << "\n";
                text << "    call lang_input_array_dyn\n";
                return;
            }

            text << "    xor rax, rax\n";
            return;
        }

        //  print(x1, x2, ...) — печатает аргументы через пробел, в конце \n.
        //  Массивы (Array/DynArray): элементы печатаются как int через пробел.
        if (callName == "print" && !fc->args.empty()) {
            for (size_t i = 0; i < fc->args.size(); i++) {
                auto argType = exprType(fc->args[i]);
                bool isArray    = argType && argType->kind == TypeKind::Array;
                bool isDynArray = argType && argType->kind == TypeKind::DynArray;

                if (isArray || isDynArray) {
                    //  rbx = базовый указатель, r12 = индекс, r13 = длина (все callee-saved → живут через call'ы)
                    int lbl = labelCounter++;
                    if (isDynArray) {
                        if (!emitDynArrayAddr(fc->args[i], "rax")) return;
                        text << "    mov rbx, [rax]\n";          //  ptr
                        text << "    mov r13, [rax + 8]\n";      //  len
                    }
                    else {
                        compileExpr(fc->args[i]);
                        text << "    mov rbx, rax\n";
                        text << "    mov r13, " << argType->arraySize << "\n";
                    }
                    text << "    xor r12, r12\n";
                    text << ".print_arr_loop_" << lbl << ":\n";
                    text << "    cmp r12, r13\n";
                    text << "    je .print_arr_end_" << lbl << "\n";
                    auto elemType = argType->elementType;
                    bool elemIsFloat = elemType && (elemType->kind == TypeKind::Float32 || elemType->kind == TypeKind::Float64);
                    int elemSz = isDynArray
                        ? (isCompositeMemoryType(elemType) ? sizeOfType(elemType) : codegenDynArrayElemSize(elemType))
                        : sizeOfType(elemType);
                    bool isSigned = elemType && (elemType->kind == TypeKind::Int8 || elemType->kind == TypeKind::Int16
                                              || elemType->kind == TypeKind::Int32 || elemType->kind == TypeKind::Int64);

                    if (elemIsFloat) {
                        if (elemSz == 4) {
                            text << "    cvtss2sd xmm0, dword [rbx + r12*4]\n";  //  float32 → float64 для print_float
                        }
                        else {
                            text << "    movsd xmm0, [rbx + r12*8]\n";
                        }
                        text << "    call print_float\n";
                    }
                    else {
                        if (elemSz == 1) {
                            if (isSigned) text << "    movsx rdi, byte [rbx + r12]\n";
                            else          text << "    movzx rdi, byte [rbx + r12]\n";
                        }
                        else if (elemSz == 2) {
                            if (isSigned) text << "    movsx rdi, word [rbx + r12*2]\n";
                            else          text << "    movzx rdi, word [rbx + r12*2]\n";
                        }
                        else if (elemSz == 4) {
                            if (isSigned) text << "    movsxd rdi, dword [rbx + r12*4]\n";
                            else          text << "    mov edi, dword [rbx + r12*4]\n";
                        }
                        else {
                            text << "    mov rdi, [rbx + r12*8]\n";
                        }
                        if (elemType && elemType->kind == TypeKind::String)    text << "    call print_string\n";
                        else if (elemType && elemType->kind == TypeKind::Bool) text << "    call print_bool\n";
                        else if (elemType && elemType->kind == TypeKind::Char) text << "    call print_char\n";
                        else                                                   text << "    call print_int\n";
                    }
                    text << "    inc r12\n";
                    text << "    cmp r12, r13\n";
                    text << "    je .print_arr_end_" << lbl << "\n";
                    text << "    call print_space\n";
                    text << "    jmp .print_arr_loop_" << lbl << "\n";
                    text << ".print_arr_end_" << lbl << ":\n";
                }
                else {
                    compileExpr(fc->args[i]);
                    bool isFloat = argType && (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);
                    if (isFloat) {
                        if (argType->kind == TypeKind::Float32) {
                            text << "    movd xmm0, eax\n";
                            text << "    cvtss2sd xmm0, xmm0\n";
                        }
                        else {
                            text << "    movq xmm0, rax\n";
                        }
                        text << "    call print_float\n";
                    }
                    else {
                        text << "    mov rdi, rax\n";
                        if (argType && argType->kind == TypeKind::Bool)        text << "    call print_bool\n";
                        else if (argType && argType->kind == TypeKind::String) text << "    call print_string\n";
                        else if (argType && argType->kind == TypeKind::Char)   text << "    call print_char\n";
                        else                                                   text << "    call print_int\n";
                    }
                }
                if (i + 1 < fc->args.size())
                    text << "    call print_space\n";
            }
            text << "    call print_newline\n";
            return;
        }
        //  push(elem, arr) — arr поддерживается как локальная DynArray или поле класса
        if (callName == "push" && fc->args.size() == 2) {
            auto arrType = exprType(fc->args[1]);
            auto elemType = arrType ? arrType->elementType : nullptr;

            if (isCompositeMemoryType(elemType)) {
                int elemSize = sizeOfType(elemType);
                if (elemSize <= 0) elemSize = 8;
                std::string storeLabel = newLabel("push_store");
                std::string doubleLabel = newLabel("push_grow_double");
                std::string allocLabel = newLabel("push_grow_alloc");
                std::string copyDoneLabel = newLabel("push_grow_copy_done");

                compileExpr(fc->args[0]);
                text << "    push rax\n";
                if (!emitDynArrayAddr(fc->args[1], "rax")) {
                    text << "    add rsp, 8\n";
                    return;
                }
                text << "    push rax\n";

                text << "    mov rbx, [rsp]\n";
                text << "    mov rcx, [rbx + 8]\n";
                text << "    cmp rcx, [rbx + 16]\n";
                text << "    jne " << storeLabel << "\n";

                text << "    mov r8, [rbx + 16]\n";
                text << "    test r8, r8\n";
                text << "    jnz " << doubleLabel << "\n";
                text << "    mov r8, 8\n";
                text << "    jmp " << allocLabel << "\n";
                text << doubleLabel << ":\n";
                text << "    shl r8, 1\n";

                text << allocLabel << ":\n";
                text << "    mov rdi, r8\n";
                text << "    imul rdi, " << elemSize << "\n";
                text << "    push r8\n";
                text << "    call lang_alloc\n";
                text << "    pop r8\n";

                text << "    mov rbx, [rsp]\n";
                text << "    mov rcx, [rbx + 8]\n";
                text << "    imul rcx, " << elemSize << "\n";
                text << "    mov rsi, [rbx]\n";
                text << "    mov rdi, rax\n";
                text << "    test rcx, rcx\n";
                text << "    jz " << copyDoneLabel << "\n";
                text << copyDoneLabel << "_loop:\n";
                text << "    mov dl, [rsi]\n";
                text << "    mov [rdi], dl\n";
                text << "    inc rsi\n";
                text << "    inc rdi\n";
                text << "    dec rcx\n";
                text << "    jnz " << copyDoneLabel << "_loop\n";
                text << copyDoneLabel << ":\n";
                text << "    mov rbx, [rsp]\n";
                text << "    mov [rbx], rax\n";
                text << "    mov [rbx + 16], r8\n";

                text << storeLabel << ":\n";
                text << "    mov rbx, [rsp]\n";
                text << "    mov rdi, [rbx]\n";
                text << "    mov rcx, [rbx + 8]\n";
                text << "    imul rcx, " << elemSize << "\n";
                text << "    add rdi, rcx\n";
                text << "    mov rsi, [rsp + 8]\n";
                emitCopy("rdi", "rsi", elemType);
                text << "    mov rbx, [rsp]\n";
                text << "    inc qword [rbx + 8]\n";
                text << "    add rsp, 16\n";
                return;
            }

            //  Вычислим элемент первым (может клобберить rdi), сохраним на стеке
            compileExpr(fc->args[0]);
            text << "    push rax\n";
            if (!emitDynArrayAddr(fc->args[1], "rdi")) {
                text << "    add rsp, 8\n";
                return;
            }
            text << "    pop rsi\n";                             //  rsi = элемент
            text << "    call lang_push\n";
            return;
        }
        //  pop(arr) → rax = последний элемент
        if (callName == "pop" && fc->args.size() == 1) {
            auto arrType = exprType(fc->args[0]);
            auto elemType = arrType ? arrType->elementType : nullptr;
            if (!emitDynArrayAddr(fc->args[0], "rdi")) return;
            text << "    mov rsi, " << fc->line << "\n";
            if (isCompositeMemoryType(elemType)) {
                text << "    mov rdx, " << sizeOfType(elemType) << "\n";
                text << "    call lang_pop_sized\n";
            }
            else {
                text << "    call lang_pop\n";
            }
            return;
        }
        if (callName == "len" && fc->args.size() == 1) {
            auto argType = exprType(fc->args[0]);
            if (argType && argType->kind == TypeKind::DynArray) {
                //  DynArray: берём len (второй qword) по адресу структуры
                if (!emitDynArrayAddr(fc->args[0], "rax")) return;
                text << "    mov rax, [rax + 8]\n";
            }
            else if (argType && argType->kind == TypeKind::Array) {
                //  Статический массив — длина известна на этапе компиляции
                text << "    mov rax, " << argType->arraySize << "\n";
            }
            else if (argType && argType->kind == TypeKind::String) {
                compileExpr(fc->args[0]);
                text << "    mov rdi, rax\n";
                text << "    call lang_strlen\n";
            }
            return;
        }

        bool isExternC = fc->isExternC || (funcInfo && funcInfo->isExternC);
        bool isVariadic = fc->isVariadic || (funcInfo && funcInfo->isVariadic);
        auto retType = fc->resolvedType ? fc->resolvedType : callReturnType(fc);
        bool returnsComposite = !isExternC && isCompositeMemoryType(retType);
        int qwordsPushed = 0;    //  Каждый аргумент кладём как один qword: scalar value или composite address.
       
        // Пушим справа налево, чтобы потом доставать слева направо.
        for (int i = (int)fc->args.size() - 1; i >= 0; --i) {
            std::shared_ptr<Type> expected = nullptr;

            if (funcInfo && i < (int)funcInfo->params.size()) {
                expected = funcInfo->params[i].type;
            }

            if (expected) {
                compileExprAs(fc->args[i], expected);
            }
            else {
                compileExpr(fc->args[i]);
            }

            // Для composite rax = address.
            // Для scalar rax = value.
            text << "    push rax\n";
            qwordsPushed++;
        }

        if (returnsComposite) {
            int retSize = sizeOfType(retType);
            if (retSize <= 0) retSize = 8;
            text << "    mov rdi, " << retSize << "\n";
            text << "    call lang_alloc\n";
            text << "    push rax\n";
            qwordsPushed++;
        }

        static const char* intRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int intIdx = 0, xmmIdx = 0;
        int qwordsConsumed = 0;    //  Сколько qword'ов мы сняли со стэка в регистры

        if (returnsComposite) {
            text << "    pop " << intRegs[intIdx++] << "\n";
            qwordsConsumed++;
        }
       
        for (size_t i = 0; i < fc->args.size(); i++) {
            std::shared_ptr<Type> argType = nullptr;

            if (funcInfo && i < funcInfo->params.size()) {
                argType = funcInfo->params[i].type;
            }
            else {
                argType = exprType(fc->args[i]);
            }

            bool isFloat = argType &&
                (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);

            // composite передаётся как один pointer/address в int-регистре.
            if (isCompositeMemoryType(argType)) {
                if (intIdx < 6) {
                    text << "    pop " << intRegs[intIdx++] << "\n";
                    qwordsConsumed++;
                }
                // Если регистров нет, аргумент уже лежит на стеке как address.
                continue;
            }

            if (isFloat && xmmIdx < 8) {
                if (argType && argType->kind == TypeKind::Float32) {
                    text << "    movd xmm" << xmmIdx++ << ", dword [rsp]\n";
                }
                else {
                    text << "    movq xmm" << xmmIdx++ << ", [rsp]\n";
                }

                text << "    add rsp, 8\n";
                qwordsConsumed++;
                continue;
            }

            if (intIdx < 6) {
                text << "    pop " << intRegs[intIdx++] << "\n";
                qwordsConsumed++;
                continue;
            }
        }

        int stackArgCount = qwordsPushed - qwordsConsumed;

        if (isExternC) {
            externCFunctions.insert(callLabel);
            if (stackArgCount % 2 != 0) text << "    sub rsp, 8\n";
            if (isVariadic) text << "    mov al, " << xmmIdx << "\n";
            text << "    call " << callLabel << "\n";
            if (stackArgCount % 2 != 0) text << "    add rsp, 8\n";
            if (stackArgCount > 0)      text << "    add rsp, " << (stackArgCount * 8) << "\n";
        } else {
            //  input/exit/panic живут в рантайме под lang_-именами (чтобы не конфликтовать с libc).
            //  Всё остальное — пользовательские/namespace/метод-функции без префикса.
            bool isRuntimeBuiltin = (callName == "input" || callName == "exit" || callName == "panic");
            if (isRuntimeBuiltin) text << "    call lang_" << callName << "\n";
            else                  text << "    call " << callLabel << "\n";
            if (stackArgCount > 0) text << "    add rsp, " << (stackArgCount * 8) << "\n";
        }
        return;
    }
    //  String / ArrayLiteral / NewExpr / ... — в следующих фазах
}

// Финальная сборка

std::expected<void, std::string> CodeGen::finalize(const std::string& outPath) {
    std::string asmPath = outPath + ".asm";
    std::string objPath = outPath + ".o";
    std::filesystem::path outputFile(outPath);
    std::filesystem::path outputDir = outputFile.parent_path();

    if (!outputDir.empty()) {
        std::error_code mkdirError;
        std::filesystem::create_directories(outputDir, mkdirError);
        if (mkdirError) {
            return std::unexpected("codegen: cannot create output directory '" + outputDir.string() + "'");
        }
    }

    auto cleanupTemps = [&]() {
        std::remove(asmPath.c_str());
        std::remove(objPath.c_str());
    };

    //  Склеиваем секции в итоговый файл
    std::ofstream out(asmPath);
    if (!out)
        return std::unexpected("codegen: cannot open '" + asmPath + "' for writing");

    if (!data.str().empty()) {
        out << "section .data\n" << data.str() << "\n";
    }
    if (!rodata.str().empty()) {
        out << "section .rodata\n" << rodata.str() << "\n";
    }
    if (!bss.str().empty()) {
        out << "section .bss\n" << bss.str() << "\n";
    }
    out << "section .text\n" << text.str();
    out << "\nsection .note.GNU-stack noalloc noexec nowrite progbits\n";
    out.close();

    //  nasm -f elf64
    std::string quotedAsmPath = shellQuote(asmPath);
    std::string quotedObjPath = shellQuote(objPath);
    std::string quotedOutPath = shellQuote(outPath);
    std::string nasmCmd = "nasm -f elf64 " + quotedAsmPath + " -o " + quotedObjPath;
    if (std::system(nasmCmd.c_str()) != 0) {
        cleanupTemps();
        return std::unexpected("codegen: nasm failed on '" + asmPath + "'");
    }

    //  Линкуем со всеми объектниками рантайма из runtime/
    //  Если используются C-функции — линкуем через gcc с libc
    std::string ldCmd;
    if (!externCFunctions.empty())
        ldCmd = "gcc -nostartfiles -no-pie " + quotedObjPath + " runtime/*.o -o " + quotedOutPath;
    else
        ldCmd = "ld " + quotedObjPath + " runtime/*.o -o " + quotedOutPath;
    if (std::system(ldCmd.c_str()) != 0) {
        cleanupTemps();
        return std::unexpected("codegen: linker failed");
    }

    //  Чистим промежуточные файлы генерации
    cleanupTemps();
    return {};
}
