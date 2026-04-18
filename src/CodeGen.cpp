#include "CodeGen.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdint>
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
        else if (c == '\n')              rodata << "\\n";
        else if (c == '\r')              rodata << "\\r";
        else if (c == '\t')              rodata << "\\t";
        else if (c < ' ' || c > '~')     rodata << "`, " << (int)c << ", `";
        else                             rodata << c;
    }
    rodata << "`, 0\n";
    return label;
}

int CodeGen::sizeOfType(const std::shared_ptr<Type>& type) {    //  Возвращает размер типа
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
        case TypeKind::Float64: return 8;

        case TypeKind::String: return 8;  //  указатель

        case TypeKind::Array: {
            int elem = sizeOfType(type->elementType);
            if (type->arraySize > 0) return elem * type->arraySize;
            return 0;
        }
        case TypeKind::DynArray: return 24; //  ptr + len + cap

        case TypeKind::Struct: 
        case TypeKind::Class: 
        case TypeKind::Alias: return 8;  
        case TypeKind::Void: return 0;
    }
    return 8;
}

int CodeGen::allocLocal(const std::string& name, const std::shared_ptr<Type>& type) {   //  Выделение локальной переменной
    int size  = sizeOfType(type);   //  Берём размер
    if (size < 8) size = 8;     //  Скалярные записи идут через qword-регистры, поэтому слот локалки — минимум 8 байт
    int align = size;
    if (align > 8) align = 8;   //  Выравнивание по 8 байт, не более
    int rem = currentFrameSize % align;                 //  Округление вверх до кратного align
    if (rem != 0) currentFrameSize += align - rem;
    currentFrameSize += size;   //  Сдвигаем указатель фрейма на размер переменной
    int offset = -currentFrameSize; //  Сохраняем смещение
    locals[name] = LocalVar{offset, type};  //  Записываем локалку
    return offset;
}

const LocalVar* CodeGen::findLocal(const std::string& name) const {
    auto it = locals.find(name);
    if (it == locals.end()) return nullptr;
    return &it->second;
}

//  Регистрирует смещение полей struct/class: каждое поле — qword по последовательным смещениям
void CodeGen::collectLayout(const std::string& name, const std::vector<StructField>& fields) {
    auto& layout = structLayouts[name];
    int offset = 0;
    for (auto& field : fields) {
        layout[field.name] = offset;
        offset += 8;
    }
    structSizes[name] = offset;
}

//  Обходит AST и регистрирует смещение для всех struct/class/namespace/export
void CodeGen::collectDecls(Stmt* decl) {
    if (!decl) return;
    if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {
        collectLayout(structDecl->name, structDecl->fields);
    }
    else if (auto* classDecl = dynamic_cast<ClassDecl*>(decl)) {
        collectLayout(classDecl->name, classDecl->fields);
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
        collectDecls(exp->decl);
    }
    else if (auto* ns  = dynamic_cast<NamespaceDecl*>(decl)) {
        for (auto* stmt : ns->decls) collectDecls(stmt);
    }
}

void CodeGen::compileDecl(Stmt* decl, const std::string& classPrefix) {
    if (!decl) return;

    if (auto* func = dynamic_cast<FuncDecl*>(decl)) {   //  Компилируем функцию
        if (func->name == "main") hasMain = true;
        std::string saveName = func->name;

        if (!classPrefix.empty()) {
            func->name = classPrefix + "_" + func->name;    //  Для функций из namespace
        }

        compileFunction(func);
        func->name = saveName;
        return;
    }

    if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {  //  Рекурсивно компилируем export функции 
        compileDecl(exp->decl, classPrefix); 
        return;
    }

    if (auto* classDecl  = dynamic_cast<ClassDecl*>(decl)) {    //  Рекурсивно компилируем методы класса
        if (classDecl->constructor) {
            compileDecl(classDecl->constructor, classDecl->name);
        }
        if (classDecl->destructor) {
            compileDecl(classDecl->destructor,  classDecl->name);
        }
        for (auto* method : classDecl->methods) {
            compileDecl(method, classDecl->name);
        }
        return;
    }

    if (auto* ns = dynamic_cast<NamespaceDecl*>(decl)) {
        std::string prefix = classPrefix.empty() ? ns->name : classPrefix + "__" + ns->name;
        for (auto* stmt : ns->decls) compileDecl(stmt, prefix);    //  Функции из namespace помечаем префиксом чтобы различить
        return;
    }
    //  StructDecl / TypeAlias / ImportDecl на верхнем уровне — ничего не генерируют
}

//  Точка входа
std::expected<void, std::string> CodeGen::generate(Program* program, const std::string& outPath) {
    text << "extern print_int\n";       //  Импорты рантайма
    text << "extern print_string\n";
    text << "extern print_bool\n";
    text << "extern print_space\n";
    text << "extern print_newline\n";
    text << "extern input\n";
    text << "extern strlen\n";
    text << "extern panic\n";
    text << "extern exit\n";
    text << "extern alloc\n";
    text << "extern free\n";
    text << "extern push\n";
    text << "extern pop\n\n";

    //  Первый проход — ставим смещение в struct/class
    for (auto* decl : program->decls) {
        collectDecls(decl);
    }

    //  Компилируем каждую функцию верхнего уровня (включая методы классов)
    hasMain = false;
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
        text << "    mov rdi, rax\n";
        text << "    call exit\n";
        text << "extern exit\n";
    } 
    else {
        text << "    mov rdi, rax\n";
        text << "    mov rax, 60\n";
        text << "    syscall\n";
    }

    return finalize(outPath);
}

//  Компиляция функции

void CodeGen::compileFunction(FuncDecl* func) {
    locals.clear();     //  Чистим текущие локалки прошлых функций
    currentFrameSize = 0;   //  Байт на функцию
    currentEndLabel = ".end_of_" + func->name;  //  Имя метки переводящей в конец, для return

    std::string symbol = func->name;
    text << "global " << symbol << "\n";    //  Определяем нашу функцию глобальной
    text << symbol << ":\n";    //  Метка для неё

    text << "    push rbp\n";   //  Начало функции
    text << "    mov rbp, rsp\n";

    for (auto& param : func->params) {  //  Выделяем память для параметров 
        allocLocal(param.name, nullptr);
    }

    int paramBytes = currentFrameSize;  
    int bodyBytes = countLocalsSize(func->body);
    int frameSize = ((paramBytes + bodyBytes + 15) / 16) * 16;  //  Новый размер фрейма функции

    if (frameSize > 0) {
        text << "    sub rsp, " << frameSize << "\n";   //  Выделяем
    }

    static const char* intArgRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int intIdx = 0, xmmIdx = 0, stackIdx = 0;
    for (const auto& param : func->params) {
        const LocalVar* localVar = findLocal(param.name);

        bool isFloat = (param.typeName == "float32" || param.typeName == "float64" || param.typeName == "float");
        if (isFloat && xmmIdx < 8) {    //  float значение передаём через xmm регистры
            text << "    movsd [rbp" << localVar->offset << "], xmm" << xmmIdx++ << "\n";
        } 
        else if (!isFloat && intIdx < 6) {  //  int значение в первые 6 регистров
            text << "    mov [rbp" << localVar->offset << "], " << intArgRegs[intIdx++] << "\n";
        } 
        else {  //  Остальные кладём в стэк
            int stackOffset = 16 + stackIdx++ * 8;
            text << "    mov rax, [rbp+" << stackOffset << "]\n";
            text << "    mov [rbp" << localVar->offset << "], rax\n";
        }
    }

    compileStmt(func->body);    //  Тело функции

    text << currentEndLabel << ":\n";   //  Конец функции
    text << "    mov rsp, rbp\n";
    text << "    pop rbp\n";
    text << "    ret\n";
    text << "\n";
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
        std::shared_ptr<Type> type = nullptr;
        
        if (var->init) {
            type = var->init->resolvedType; //  У обычной переменной берём тип
        }
        else if (var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]") {
            auto arrType = std::make_shared<Type>();
            arrType->kind = TypeKind::DynArray;
            type = arrType;
        }
        int size = sizeOfType(type);
        if (size < 8) size = 8;     //  Минимум 8 — слоты локалок всегда qword-выровнены
        return size;
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
        std::shared_ptr<Type> type = nullptr;
        if (var->init) {
            type = var->init->resolvedType; //  Тип берём из init (семантик уже проставил resolvedType)
        }
        else if (var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]") {
            auto arrType = std::make_shared<Type>();
            arrType->kind = TypeKind::DynArray;
            type = arrType;
        }

        int off = allocLocal(var->name, type);
        if (var->init) {
            compileExpr(var->init);
            text << "    mov [rbp" << off << "], rax\n";
        }
        else if (type && type->kind == TypeKind::DynArray) {    //  Пустой DynArray: ptr=0, len=0, cap=0 — иначе мусор в rbp-slot ломает lang_push
            text << "    mov qword [rbp" << off << "], 0\n";
            text << "    mov qword [rbp" << (off + 8) << "], 0\n";
            text << "    mov qword [rbp" << (off + 16) << "], 0\n";
        }
        return;
    }
    if (auto* assign = dynamic_cast<Assign*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            const LocalVar* localVar = findLocal(id->name);
            if (!localVar) return;
            compileExpr(assign->value);
            text << "    mov [rbp" << localVar->offset << "], rax\n";   //  Закидываем в локалку по сдвигу
            return;
        }
        if (auto* fieldAccess = dynamic_cast<FieldAccess*>(assign->target)) {
            compileExpr(fieldAccess->object);
            text << "    push rax\n";
            compileExpr(assign->value);
            text << "    pop rbx\n";
            int off = 0;
            std::shared_ptr<Type> type = nullptr;
            if (fieldAccess->object) type = fieldAccess->object->resolvedType;
            if (type) {
                auto it = structLayouts.find(type->name);
                if (it != structLayouts.end() && it->second.count(fieldAccess->field))
                    off = it->second[fieldAccess->field];
            }
            text << "    mov [rbx+" << off << "], rax\n";
            return;
        }
        if (auto* aa = dynamic_cast<ArrayAccess*>(assign->target)) {
            compileExpr(aa->object);
            text << "    push rax\n";
            compileExpr(aa->index);
            text << "    push rax\n";
            compileExpr(assign->value);
            text << "    pop rbx\n";                         //  индекс
            text << "    pop rcx\n";                         //  база
            text << "    mov [rcx + rbx*8], rax\n";
            return;
        }
        return;
    }
    if (auto* ds = dynamic_cast<DeleteStmt*>(stmt)) {
        compileExpr(ds->value);
        text << "    mov rdi, rax\n";
        text << "    call lang_free\n";
        return;
    }
    if (auto* es = dynamic_cast<ExprStmt*>(stmt)) {
        compileExpr(es->expr);
        return;
    }
    if (auto* ret = dynamic_cast<Return*>(stmt)) {
        if (ret->value) compileExpr(ret->value);         //  Значение в rax
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

void CodeGen::compileExpr(Expr* expr) {
    if (!expr) return;
    if (auto* num = dynamic_cast<Number*>(expr)) {  //  Встретили число
        if (num->isFloat) {  //  Кодируем double как raw-биты в rax
            double value = num->value;
            uint64_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            text << "    mov rax, " << bits << "\n";
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
    if (auto* str = dynamic_cast<String*>(expr)) {  //  Строка
        std::string label = internString(str->value);   //  пишем строку в rodata
        text << "    lea rax, [rel " << label << "]\n"; //  берём адрес строки по её метке
        return;
    }
    if (auto* arrayLit = dynamic_cast<ArrayLiteral*>(expr)) {   //  Литерал массива 
        std::string label = "arr" + std::to_string(arrayCounter++);     //  Метка в data
        data << label << ": dq ";   //  Помещаем в data

        for (size_t i = 0; i < arrayLit->elements.size(); ++i) {
            auto* elem = arrayLit->elements[i];
            if (auto* num = dynamic_cast<Number*>(elem)) {
                if (num->isFloat) {
                    double value = num->value;
                    uint64_t bits;
                    std::memcpy(&bits, &value, sizeof(bits));    //  Raw-биты double: NASM иначе спутает `5.0` с int 5
                    data << bits;
                }
                else {
                    data << (long long)num->value;
                }
            }
            else if (auto* boolLit = dynamic_cast<Bool*>(elem)) {
                if (boolLit->value) {
                    data << 1;
                }
                else {
                    data << 0;
                }
            }
            else if (auto* str = dynamic_cast<String*>(elem)) {
                data << internString(str->value);            //  Метка из .rodata — в dq попадёт её адрес
            }
            else if (auto* un = dynamic_cast<Unary*>(elem)) {
                auto* num = dynamic_cast<Number*>(un->operand);
                if (un->op == Operand::UnaryMinus && num) {
                    if (num->isFloat) {
                        double value = -num->value;
                        uint64_t bits;
                        std::memcpy(&bits, &value, sizeof(bits));
                        data << bits;
                    } else {
                        data << -(long long)num->value;
                    }
                } else {
                    data << "0";                             //  +x, !x и прочее — в следующих фазах
                }
            }
            else {
                data << "0";                                 //  сложные элементы — в следующих фазах
            }
            if (i + 1 < arrayLit->elements.size()) data << ",";
        }
        data << "\n";
        text << "    lea rax, [rel " << label << "]\n";
        return;
    }
    if (auto* sl = dynamic_cast<StructLiteral*>(expr)) {    //  Аллоцируем объект в куче, пишем поля по их смещениям
        int size;
        if (structSizes.count(sl->name)) {
            size = structSizes[sl->name];
        }
        else {
            size = (int)sl->fields.size() * 8;
        }
        text << "    mov rdi, " << size << "\n";
        text << "    call lang_alloc\n";
        text << "    push rax\n";                            //  сохраним указатель на объект
        auto& layout = structLayouts[sl->name];
        for (auto& fi : sl->fields) {
            compileExpr(fi.value);
            text << "    mov rbx, [rsp]\n";                  //  базовый адрес объекта
            int off = 0;
            if (layout.count(fi.name)) {
                off = layout[fi.name];
            }
            text << "    mov [rbx+" << off << "], rax\n";
        }
        text << "    pop rax\n";                             //  результат литерала — указатель
        return;
    }
    if (auto* fa = dynamic_cast<FieldAccess*>(expr)) {
        compileExpr(fa->object);
        int off = 0;
        //  Ищем поле: у object должен быть resolvedType со struct/class/alias
        std::shared_ptr<Type> type = nullptr;
        if (fa->object) {
            type = fa->object->resolvedType;
        }
        if (type) {
            const std::string& typeName = type->name;
            auto it = structLayouts.find(typeName);
            if (it != structLayouts.end() && it->second.count(fa->field)) {
                off = it->second[fa->field];
            }
        }
        text << "    mov rax, [rax+" << off << "]\n";
        return;
    }
    if (auto* newExpr = dynamic_cast<NewExpr*>(expr)) {
        //  new ClassName(args) — alloc + вызов конструктора с this в rdi
        int size = 8;
        if (structSizes.count(newExpr->className)) {
            size = structSizes[newExpr->className];
        }
        text << "    mov rdi, " << size << "\n";
        text << "    call lang_alloc\n";
        text << "    push rax\n";                            //  this
        //  Аргументы: пока поддерживаем 0–5 (this занимает rdi)
        for (int i = (int)newExpr->args.size() - 1; i >= 0; --i) {
            compileExpr(newExpr->args[i]);
            text << "    push rax\n";
        }
        static const char* argRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};
        
        for (size_t i = 0; i < newExpr->args.size() && i < 5; ++i) {
            text << "    pop " << argRegs[i] << "\n";
        }
        text << "    mov rdi, [rsp]\n";                      //  this снова в rdi
        text << "    call lang_" << newExpr->className << "__" << newExpr->className << "\n";
        text << "    pop rax\n";                             //  результат new — указатель
        return;
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(expr)) {
        //  Базовый адрес → push, индекс → rbx, pop base, читаем qword-элемент
        compileExpr(aa->object);
        text << "    push rax\n";
        compileExpr(aa->index);
        text << "    mov rbx, rax\n";
        text << "    pop rax\n";
        text << "    mov rax, [rax + rbx*8]\n";
        return;
    }
    if (auto* castExpr = dynamic_cast<CastExpr*>(expr)) {
        compileExpr(castExpr->value);
        std::shared_ptr<Type> srcT = nullptr;
        if (castExpr->value) {
            srcT = castExpr->value->resolvedType;
        }
        bool srcFloat = srcT && (srcT->kind == TypeKind::Float32 || srcT->kind == TypeKind::Float64);
        //  Тип назначения — строка, сопоставим ключевые варианты
        const std::string& dstType = castExpr->targetType;
        bool dstFloat = (dstType == "float32" || dstType == "float64" || dstType == "float");
        if (srcFloat && !dstFloat) {
            text << "    movq xmm0, rax\n";
            text << "    cvttsd2si rax, xmm0\n";
        }
        else if (!srcFloat && dstFloat) {
            text << "    cvtsi2sd xmm0, rax\n";
            text << "    movq rax, xmm0\n";
        }
        //  int↔int: хранение в qword — приведение без инструкций
        return;
    }
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        const LocalVar* lv = findLocal(id->name);
        if (!lv) return;                             //  Семантик уже проверил существование
        text << "    mov rax, [rbp" << lv->offset << "]\n";
        return;
    }
    if (auto* unary = dynamic_cast<Unary*>(expr)) {
        if (unary->op == Operand::UnaryPlus) {
            compileExpr(unary->operand);
            return;
        }
        if (unary->op == Operand::UnaryMinus) {
            compileExpr(unary->operand);
            text << "    neg rax\n";
            return;
        }
        if (unary->op == Operand::Increment || unary->op == Operand::Decrement) {
            //  ++/-- на идентификаторе: читаем → меняем в памяти → возвращаем новое значение
            if (auto* id = dynamic_cast<Identifier*>(unary->operand)) {
                const LocalVar* lv = findLocal(id->name);
                if (!lv) return;
                if (unary->op == Operand::Increment)
                    text << "    inc qword [rbp" << lv->offset << "]\n";
                else
                    text << "    dec qword [rbp" << lv->offset << "]\n";
                text << "    mov rax, [rbp" << lv->offset << "]\n";
            }
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

        //  Float-арифметика: вычисляем в xmm0/xmm1, результат обратно в rax через movq
        bool isFloat = false;
        if (bin->left && bin->left->resolvedType) {
            auto kind = bin->left->resolvedType->kind;
            isFloat = (kind == TypeKind::Float32 || kind == TypeKind::Float64);
        }
        if (isFloat) {
            compileExpr(bin->left);
            text << "    push rax\n";
            compileExpr(bin->right);
            text << "    movq xmm1, rax\n";
            text << "    pop rax\n";
            text << "    movq xmm0, rax\n";
            switch (bin->op) {
                case Operand::Add: text << "    addsd xmm0, xmm1\n"; break;
                case Operand::Sub: text << "    subsd xmm0, xmm1\n"; break;
                case Operand::Mul: text << "    mulsd xmm0, xmm1\n"; break;
                case Operand::Div: text << "    divsd xmm0, xmm1\n"; break;
                case Operand::Less: 
                case Operand::Greater:
                case Operand::LessEqual:
                case Operand::GreaterEqual:
                case Operand::EqualEqual: 
                case Operand::NotEqual: {
                    text << "    ucomisd xmm0, xmm1\n";
                    const char* cc = "e";
                    switch (bin->op) {
                        case Operand::Less:         cc = "b";  break;
                        case Operand::Greater:      cc = "a";  break;
                        case Operand::LessEqual:    cc = "be"; break;
                        case Operand::GreaterEqual: cc = "ae"; break;
                        case Operand::EqualEqual:   cc = "e";  break;
                        case Operand::NotEqual:     cc = "ne"; break;
                        default: break;
                    }
                    text << "    set" << cc << " al\n";
                    text << "    movzx rax, al\n";
                    return;
                }
                default: break;
            }
            text << "    movq rax, xmm0\n";
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
        if (bin->left && bin->left->resolvedType) {
            auto kind = bin->left->resolvedType->kind;
            isUnsigned = (kind == TypeKind::Uint8  || kind == TypeKind::Uint16 || kind == TypeKind::Uint32 || kind == TypeKind::Uint64);
        }

        switch (bin->op) {
            case Operand::Add: text << "    add rax, rbx\n"; break;
            case Operand::Sub: text << "    sub rax, rbx\n"; break;
            case Operand::Mul:
                if (isUnsigned) text << "    mul rbx\n";           //  rdx:rax = rax * rbx
                else            text << "    imul rax, rbx\n";
                break;
            case Operand::Div:
                if (isUnsigned) text << "    xor rdx, rdx\n    div rbx\n";
                else            text << "    cqo\n    idiv rbx\n";
                break;
            case Operand::Mod:
                if (isUnsigned) text << "    xor rdx, rdx\n    div rbx\n";
                else            text << "    cqo\n    idiv rbx\n";
                text << "    mov rax, rdx\n";
                break;
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
        std::string callName;
        if (auto* id = dynamic_cast<Identifier*>(fc->callee)) callName = id->name;
        else if (auto* na = dynamic_cast<NamespaceAccess*>(fc->callee))
            callName = na->nameSpace + "__" + na->member;
        else return;                                 //  Методы экземпляра — через FieldAccess, позже

        //  Встроенные функции
        //  print(x1, x2, ...) — печатает аргументы через пробел, в конце \n
        if (callName == "print" && !fc->args.empty()) {
            for (size_t i = 0; i < fc->args.size(); i++) {
                compileExpr(fc->args[i]);
                text << "    mov rdi, rax\n";
                auto argType = fc->args[i]->resolvedType;
                if (argType && argType->kind == TypeKind::Bool)        text << "    call print_bool\n";
                else if (argType && argType->kind == TypeKind::String) text << "    call print_string\n";
                else                                                   text << "    call print_int\n";
                if (i + 1 < fc->args.size())
                    text << "    call print_space\n";
            }
            text << "    call print_newline\n";
            return;
        }
        //  push(arr, elem) — arr поддерживается только как Identifier DynArray-локалки
        if (callName == "push" && fc->args.size() == 2) {
            auto* id = dynamic_cast<Identifier*>(fc->args[0]);
            if (!id) return;
            const LocalVar* lv = findLocal(id->name);
            if (!lv) return;
            //  Вычислим элемент первым (может клобберить rdi), сохраним на стеке
            compileExpr(fc->args[1]);
            text << "    push rax\n";
            text << "    lea rdi, [rbp" << lv->offset << "]\n";  //  rdi = адрес DynArray
            text << "    pop rsi\n";                             //  rsi = элемент
            text << "    call lang_push\n";
            return;
        }
        //  pop(arr) → rax = последний элемент
        if (callName == "pop" && fc->args.size() == 1) {
            auto* id = dynamic_cast<Identifier*>(fc->args[0]);
            if (!id) return;
            const LocalVar* lv = findLocal(id->name);
            if (!lv) return;
            text << "    lea rdi, [rbp" << lv->offset << "]\n";
            text << "    call lang_pop\n";
            return;
        }
        if (callName == "len" && fc->args.size() == 1) {
            auto argType = fc->args[0]->resolvedType;
            if (argType && argType->kind == TypeKind::DynArray) {
                //  DynArray: нужен адрес самой структуры, а не её первого qword — берём lea из локалки
                auto* id = dynamic_cast<Identifier*>(fc->args[0]);
                if (!id) return;
                const LocalVar* lv = findLocal(id->name);
                if (!lv) return;
                text << "    mov rax, [rbp" << (lv->offset + 8) << "]\n";
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

        int argCount = (int)fc->args.size();
        for (int i = argCount - 1; i >= 0; --i) {
            compileExpr(fc->args[i]);
            text << "    push rax\n";
        }

        static const char* intRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int intIdx = 0, xmmIdx = 0;
        for (int i = 0; i < argCount; ++i) {
            auto& argType = fc->args[i]->resolvedType;
            bool isFloat = argType && (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);
            if (isFloat && xmmIdx < 8) {
                text << "    movsd xmm" << xmmIdx++ << ", [rsp]\n";
                text << "    add rsp, 8\n";
            } else if (!isFloat && intIdx < 6) {
                text << "    pop " << intRegs[intIdx++] << "\n";
            }
        }
        int stackArgCount = argCount - intIdx - xmmIdx;

        if (fc->isExternC) {
            externCFunctions.insert(callName);
            if (stackArgCount % 2 != 0) text << "    sub rsp, 8\n";
            if (fc->isVariadic) text << "    mov al, " << xmmIdx << "\n";
            text << "    call " << callName << "\n";
            if (stackArgCount % 2 != 0) text << "    add rsp, 8\n";
            if (stackArgCount > 0)      text << "    add rsp, " << (stackArgCount * 8) << "\n";
        } else {
            text << "    call lang_" << callName << "\n";
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
    out.close();

    //  nasm -f elf64
    std::string nasmCmd = "nasm -f elf64 " + asmPath + " -o " + objPath;
    if (std::system(nasmCmd.c_str()) != 0)
        return std::unexpected("codegen: nasm failed on '" + asmPath + "'");

    //  Линкуем со всеми объектниками рантайма из runtime/
    //  Если используются C-функции — линкуем через gcc с libc
    std::string ldCmd;
    if (!externCFunctions.empty())
        ldCmd = "gcc -nostartfiles -no-pie " + objPath + " runtime/*.o -o " + outPath;
    else
        ldCmd = "ld " + objPath + " runtime/*.o -o " + outPath;
    if (std::system(ldCmd.c_str()) != 0)
        return std::unexpected("codegen: linker failed");

    //  Чистим промежуточный объектник
    std::remove(objPath.c_str());
    return {};
}
