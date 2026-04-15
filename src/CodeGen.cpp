#include "CodeGen.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <functional>

//  ─── Базовые helper'ы вывода ───

void CodeGen::emit(const std::string& line) {
    text << "    " << line << "\n";
}

void CodeGen::emitLabel(const std::string& label) {
    text << label << ":\n";
}

std::string CodeGen::newLabel(const std::string& hint) {
    return "." + hint + std::to_string(labelCounter++);
}

std::string CodeGen::internString(const std::string& s) {
    auto it = stringPool.find(s);
    if (it != stringPool.end()) return it->second;

    std::string label = "str" + std::to_string(stringCounter++);
    stringPool[s] = label;

    //  Побайтовая запись — NASM-формат `db b1, b2, ..., 0`
    rodata << label << ": db ";
    for (unsigned char c : s)
        rodata << (int)c << ",";
    rodata << "0\n";
    return label;
}

//  ─── Размер типа ───

int CodeGen::sizeOfType(const std::shared_ptr<Type>& t) {
    if (!t) return 8;
    switch (t->kind) {
        case TypeKind::Int8:  case TypeKind::Uint8:  case TypeKind::Bool:  case TypeKind::Char:  return 1;
        case TypeKind::Int16: case TypeKind::Uint16:                                             return 2;
        case TypeKind::Int32: case TypeKind::Uint32: case TypeKind::Float32:                     return 4;
        case TypeKind::Int64: case TypeKind::Uint64: case TypeKind::Float64:                     return 8;
        case TypeKind::String:                                                                   return 8;  //  указатель
        case TypeKind::Array: {
            int elem = sizeOfType(t->elementType);
            return elem * (t->arraySize > 0 ? t->arraySize : 0);
        }
        case TypeKind::DynArray:                                                                 return 24; //  ptr + len + cap
        case TypeKind::Struct: case TypeKind::Class: case TypeKind::Alias:                       return 8;  //  placeholder (посчитаем в Фазе 13)
        case TypeKind::Void:                                                                     return 0;
    }
    return 8;
}

//  ─── Локальные переменные ───

int CodeGen::allocLocal(const std::string& name, const std::shared_ptr<Type>& type) {
    //  В текущей модели каждая локалка — qword (скаляр или указатель на heap/.data).
    //  Это синхронно с countLocalsSize, который тоже считает 8 байт на VarDecl.
    currentFrameSize += 8;
    int offset = -currentFrameSize;             //  [rbp - currentFrameSize]
    locals[name] = LocalVar{offset, type};
    return offset;
}

const LocalVar* CodeGen::findLocal(const std::string& name) const {
    auto it = locals.find(name);
    return it == locals.end() ? nullptr : &it->second;
}

//  ─── Точка входа ───

std::expected<void, std::string> CodeGen::generate(Program* program, const std::string& outPath) {
    //  Импорты рантайма
    text << "extern print_int\n";
    text << "extern print_string\n";
    text << "extern print_bool\n";
    text << "extern lang_strlen\n";
    text << "extern lang_panic\n";
    text << "extern lang_alloc\n";
    text << "extern lang_free\n\n";

    //  Пролог точки входа: _start → call lang_main → sys_exit(rax)
    text << "global _start\n";
    text << "_start:\n";
    text << "    call lang_main\n";
    text << "    mov rdi, rax\n";
    text << "    mov rax, 60\n";
    text << "    syscall\n\n";

    //  Первый проход — соберём layout'ы структур и классов (каждое поле = qword)
    auto collectLayout = [&](const std::string& name, const std::vector<StructField>& fields) {
        auto& layout = structLayouts[name];
        int offset = 0;
        for (auto& f : fields) {
            layout[f.name] = offset;
            offset += 8;
        }
        structSizes[name] = offset;
    };
    std::function<void(Stmt*)> collectDecls = [&](Stmt* d) {
        if (!d) return;
        if (auto* sd = dynamic_cast<StructDecl*>(d)) collectLayout(sd->name, sd->fields);
        else if (auto* cd = dynamic_cast<ClassDecl*>(d)) collectLayout(cd->name, cd->fields);
        else if (auto* exp = dynamic_cast<ExportDecl*>(d)) collectDecls(exp->decl);
        else if (auto* ns = dynamic_cast<NamespaceDecl*>(d))
            for (auto* s : ns->decls) collectDecls(s);
    };
    for (auto* decl : program->decls) collectDecls(decl);

    //  Компилируем каждую функцию верхнего уровня (включая методы классов)
    bool hasMain = false;
    std::function<void(Stmt*, const std::string&)> compileDecl = [&](Stmt* d, const std::string& classPrefix) {
        if (!d) return;
        FuncDecl* fn = dynamic_cast<FuncDecl*>(d);
        if (fn) {
            if (fn->name == "main") hasMain = true;
            //  classPrefix пустой для обычных функций
            std::string saveName = fn->name;
            if (!classPrefix.empty()) fn->name = classPrefix + "__" + fn->name;
            compileFunction(fn);
            fn->name = saveName;
            return;
        }
        if (auto* exp = dynamic_cast<ExportDecl*>(d)) { compileDecl(exp->decl, classPrefix); return; }
        if (auto* cd  = dynamic_cast<ClassDecl*>(d)) {
            if (cd->constructor) compileDecl(cd->constructor, cd->name);
            if (cd->destructor)  compileDecl(cd->destructor,  cd->name);
            for (auto* m : cd->methods) compileDecl(m, cd->name);
            return;
        }
        if (auto* ns  = dynamic_cast<NamespaceDecl*>(d)) {
            //  Пространства имён: все декларации получают префикс ns__
            std::string p = classPrefix.empty() ? ns->name : (classPrefix + "__" + ns->name);
            for (auto* s : ns->decls) compileDecl(s, p);
            return;
        }
        //  TypeAlias / ImportDecl / StructDecl на верхнем уровне — ничего не генерируют
        if (dynamic_cast<StructDecl*>(d))   return;
        if (dynamic_cast<TypeAlias*>(d))    return;
        if (dynamic_cast<ImportDecl*>(d))   return;
    };
    for (auto* decl : program->decls) compileDecl(decl, "");

    if (!hasMain)
        return std::unexpected("codegen: no 'main' function defined");

    return finalize(outPath);
}

//  ─── Компиляция функции ───

void CodeGen::compileFunction(FuncDecl* fn) {
    locals.clear();
    currentFrameSize = 0;
    currentEpilogLabel = ".epilog_" + fn->name;

    std::string symbol = "lang_" + fn->name;
    text << "global " << symbol << "\n";
    emitLabel(symbol);

    //  Пролог
    emit("push rbp");
    emit("mov rbp, rsp");

    //  Слоты для параметров (первые 6 — из регистров)
    for (auto& p : fn->params)
        allocLocal(p.name, nullptr);

    int paramBytes = currentFrameSize;
    int bodyBytes  = countLocalsSize(fn->body);
    int frameSize  = (paramBytes + bodyBytes + 15) & ~15;    //  Выравнивание до 16 байт

    if (frameSize > 0)
        emit("sub rsp, " + std::to_string(frameSize));

    //  Сохраняем параметры из регистров в их слоты
    static const char* argRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    for (size_t i = 0; i < fn->params.size() && i < 6; ++i) {
        const LocalVar* lv = findLocal(fn->params[i].name);
        emit("mov [rbp" + std::to_string(lv->offset) + "], " + argRegs[i]);
    }

    //  Тело
    compileStmt(fn->body);

    //  Эпилог
    emitLabel(currentEpilogLabel);
    emit("mov rsp, rbp");
    emit("pop rbp");
    emit("ret");
    text << "\n";
}

int CodeGen::countLocalsSize(Stmt* s) {
    if (!s) return 0;
    if (auto* b = dynamic_cast<Block*>(s)) {
        int sum = 0;
        for (auto* st : b->statements) sum += countLocalsSize(st);
        return sum;
    }
    if (dynamic_cast<VarDecl*>(s))
        return 8;                                    //  Точный размер — в фазе 3 (нужен resolvedType)
    if (auto* i = dynamic_cast<If*>(s))
        return countLocalsSize(i->thenBranch) + countLocalsSize(i->elseBranch);
    if (auto* w = dynamic_cast<While*>(s))
        return countLocalsSize(w->body);
    return 0;
}

void CodeGen::compileStmt(Stmt* s) {
    if (!s) return;
    if (auto* b = dynamic_cast<Block*>(s)) {
        for (auto* st : b->statements) compileStmt(st);
        return;
    }
    if (auto* v = dynamic_cast<VarDecl*>(s)) {
        //  Тип берём из init (семантик уже проставил resolvedType). Точный layout — позже.
        auto t = v->init ? v->init->resolvedType : nullptr;
        int off = allocLocal(v->name, t);
        if (v->init) {
            compileExpr(v->init);
            emit("mov [rbp" + std::to_string(off) + "], rax");
        }
        return;
    }
    if (auto* a = dynamic_cast<Assign*>(s)) {
        if (auto* id = dynamic_cast<Identifier*>(a->target)) {
            const LocalVar* lv = findLocal(id->name);
            if (!lv) return;
            compileExpr(a->value);
            emit("mov [rbp" + std::to_string(lv->offset) + "], rax");
            return;
        }
        if (auto* fa = dynamic_cast<FieldAccess*>(a->target)) {
            compileExpr(fa->object);
            emit("push rax");
            compileExpr(a->value);
            emit("pop rbx");
            int off = 0;
            auto t = fa->object ? fa->object->resolvedType : nullptr;
            if (t) {
                auto it = structLayouts.find(t->name);
                if (it != structLayouts.end() && it->second.count(fa->field))
                    off = it->second[fa->field];
            }
            emit("mov [rbx+" + std::to_string(off) + "], rax");
            return;
        }
        if (auto* aa = dynamic_cast<ArrayAccess*>(a->target)) {
            compileExpr(aa->object);
            emit("push rax");
            compileExpr(aa->index);
            emit("push rax");
            compileExpr(a->value);
            emit("pop rbx");                         //  индекс
            emit("pop rcx");                         //  база
            emit("mov [rcx + rbx*8], rax");
            return;
        }
        return;
    }
    if (auto* ds = dynamic_cast<DeleteStmt*>(s)) {
        compileExpr(ds->value);
        emit("mov rdi, rax");
        emit("call lang_free");
        return;
    }
    if (auto* es = dynamic_cast<ExprStmt*>(s)) {
        compileExpr(es->expr);
        return;
    }
    if (auto* r = dynamic_cast<Return*>(s)) {
        if (r->value) compileExpr(r->value);         //  Значение в rax
        emit("jmp " + currentEpilogLabel);
        return;
    }
    if (auto* i = dynamic_cast<If*>(s)) {
        std::string elseL = newLabel("else");
        std::string endL  = newLabel("endif");
        compileExpr(i->condition);
        emit("test rax, rax");
        emit("jz " + (i->elseBranch ? elseL : endL));
        compileStmt(i->thenBranch);
        if (i->elseBranch) {
            emit("jmp " + endL);
            emitLabel(elseL);
            compileStmt(i->elseBranch);
        }
        emitLabel(endL);
        return;
    }
    if (auto* w = dynamic_cast<While*>(s)) {
        std::string startL = newLabel("while");
        std::string endL   = newLabel("endwhile");
        loopStack.push_back({endL, startL});
        emitLabel(startL);
        compileExpr(w->condition);
        emit("test rax, rax");
        emit("jz " + endL);
        compileStmt(w->body);
        emit("jmp " + startL);
        emitLabel(endL);
        loopStack.pop_back();
        return;
    }
    if (dynamic_cast<Break*>(s)) {
        if (!loopStack.empty())
            emit("jmp " + loopStack.back().breakLabel);
        return;
    }
    if (dynamic_cast<Continue*>(s)) {
        if (!loopStack.empty())
            emit("jmp " + loopStack.back().continueLabel);
        return;
    }
}

void CodeGen::compileExpr(Expr* e) {
    if (!e) return;
    if (auto* n = dynamic_cast<Number*>(e)) {
        if (n->isFloat) {
            //  Кодируем double как raw-биты в rax; в xmm0 — через movq
            double d = n->value;
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(bits));
            emit("mov rax, " + std::to_string(bits));
            return;
        }
        long long v = (long long)n->value;
        emit("mov rax, " + std::to_string(v));
        return;
    }
    if (auto* b = dynamic_cast<Bool*>(e)) {
        emit("mov rax, " + std::to_string(b->value ? 1 : 0));
        return;
    }
    if (auto* s = dynamic_cast<String*>(e)) {
        //  Строковый литерал: регистрируем в .rodata, грузим адрес
        std::string label = internString(s->value);
        emit("lea rax, [rel " + label + "]");
        return;
    }
    if (auto* al = dynamic_cast<ArrayLiteral*>(e)) {
        //  Литерал массива — размещаем в .data (каждый элемент qword)
        //  Поддерживаются только численные/булевы литералы элементов
        std::string label = "arr" + std::to_string(arrayCounter++);
        data << label << ": dq ";
        for (size_t i = 0; i < al->elements.size(); ++i) {
            auto* el = al->elements[i];
            if (auto* num = dynamic_cast<Number*>(el)) {
                data << (long long)num->value;
            } else if (auto* bl = dynamic_cast<Bool*>(el)) {
                data << (bl->value ? 1 : 0);
            } else {
                data << "0";                          //  сложные элементы — в следующих фазах
            }
            if (i + 1 < al->elements.size()) data << ",";
        }
        data << "\n";
        emit("lea rax, [rel " + label + "]");
        return;
    }
    if (auto* sl = dynamic_cast<StructLiteral*>(e)) {
        //  Аллоцируем объект в куче, пишем поля по их смещениям
        int size = structSizes.count(sl->name) ? structSizes[sl->name] : (int)sl->fields.size() * 8;
        emit("mov rdi, " + std::to_string(size));
        emit("call lang_alloc");
        emit("push rax");                            //  сохраним указатель на объект
        auto& layout = structLayouts[sl->name];
        for (auto& fi : sl->fields) {
            compileExpr(fi.value);
            emit("mov rbx, [rsp]");                  //  базовый адрес объекта
            int off = layout.count(fi.name) ? layout[fi.name] : 0;
            emit("mov [rbx+" + std::to_string(off) + "], rax");
        }
        emit("pop rax");                             //  результат литерала — указатель
        return;
    }
    if (auto* fa = dynamic_cast<FieldAccess*>(e)) {
        compileExpr(fa->object);
        int off = 0;
        //  Ищем поле: у object должен быть resolvedType со struct/class/alias
        auto t = fa->object ? fa->object->resolvedType : nullptr;
        if (t) {
            const std::string& typeName = t->name;
            auto it = structLayouts.find(typeName);
            if (it != structLayouts.end() && it->second.count(fa->field))
                off = it->second[fa->field];
        }
        emit("mov rax, [rax+" + std::to_string(off) + "]");
        return;
    }
    if (auto* ne = dynamic_cast<NewExpr*>(e)) {
        //  new ClassName(args) — alloc + вызов конструктора с this в rdi
        int size = structSizes.count(ne->className) ? structSizes[ne->className] : 8;
        emit("mov rdi, " + std::to_string(size));
        emit("call lang_alloc");
        emit("push rax");                            //  this
        //  Аргументы: пока поддерживаем 0–5 (this занимает rdi)
        for (int i = (int)ne->args.size() - 1; i >= 0; --i) {
            compileExpr(ne->args[i]);
            emit("push rax");
        }
        static const char* argRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};
        for (size_t i = 0; i < ne->args.size() && i < 5; ++i)
            emit("pop " + std::string(argRegs[i]));
        emit("mov rdi, [rsp]");                      //  this снова в rdi
        emit("call lang_" + ne->className + "__" + ne->className);
        emit("pop rax");                             //  результат new — указатель
        return;
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(e)) {
        //  Базовый адрес → push, индекс → rbx, pop base, читаем qword-элемент
        compileExpr(aa->object);
        emit("push rax");
        compileExpr(aa->index);
        emit("mov rbx, rax");
        emit("pop rax");
        emit("mov rax, [rax + rbx*8]");
        return;
    }
    if (auto* c = dynamic_cast<CastExpr*>(e)) {
        compileExpr(c->value);
        auto srcT = c->value ? c->value->resolvedType : nullptr;
        bool srcFloat = srcT && (srcT->kind == TypeKind::Float32 || srcT->kind == TypeKind::Float64);
        //  Тип назначения — строка, сопоставим ключевые варианты
        const std::string& d = c->targetType;
        bool dstFloat = (d == "float32" || d == "float64" || d == "float");
        if (srcFloat && !dstFloat) {
            emit("movq xmm0, rax");
            emit("cvttsd2si rax, xmm0");
        } else if (!srcFloat && dstFloat) {
            emit("cvtsi2sd xmm0, rax");
            emit("movq rax, xmm0");
        }
        //  int↔int: хранение в qword — приведение без инструкций
        return;
    }
    if (auto* id = dynamic_cast<Identifier*>(e)) {
        const LocalVar* lv = findLocal(id->name);
        if (!lv) return;                             //  Семантик уже проверил существование
        emit("mov rax, [rbp" + std::to_string(lv->offset) + "]");
        return;
    }
    if (auto* u = dynamic_cast<Unary*>(e)) {
        if (u->op == Operand::UnaryPlus) {
            compileExpr(u->operand);
            return;
        }
        if (u->op == Operand::UnaryMinus) {
            compileExpr(u->operand);
            emit("neg rax");
            return;
        }
        if (u->op == Operand::Increment || u->op == Operand::Decrement) {
            //  ++/-- на идентификаторе: читаем → меняем в памяти → возвращаем новое значение
            if (auto* id = dynamic_cast<Identifier*>(u->operand)) {
                const LocalVar* lv = findLocal(id->name);
                if (!lv) return;
                std::string mem = "[rbp" + std::to_string(lv->offset) + "]";
                emit(std::string(u->op == Operand::Increment ? "inc" : "dec") + " qword " + mem);
                emit("mov rax, " + mem);
            }
            return;
        }
        if (u->op == Operand::Not) {
            compileExpr(u->operand);
            emit("xor rax, 1");
            return;
        }
        return;
    }
    if (auto* bin = dynamic_cast<Binary*>(e)) {
        //  Short-circuit для && и || — нельзя вычислять оба операнда заранее
        if (bin->op == Operand::And || bin->op == Operand::Or) {
            bool isAnd = (bin->op == Operand::And);
            std::string shortL = newLabel(isAnd ? "and_false" : "or_true");
            std::string endL   = newLabel("logic_end");

            compileExpr(bin->left);
            emit("test rax, rax");
            emit(std::string(isAnd ? "jz " : "jnz ") + shortL);
            compileExpr(bin->right);
            emit("test rax, rax");
            emit(std::string(isAnd ? "jz " : "jnz ") + shortL);
            emit(isAnd ? "mov rax, 1" : "xor rax, rax");
            emit("jmp " + endL);
            emitLabel(shortL);
            emit(isAnd ? "xor rax, rax" : "mov rax, 1");
            emitLabel(endL);
            return;
        }

        //  Float-арифметика: вычисляем в xmm0/xmm1, результат обратно в rax через movq
        bool isFloat = false;
        if (bin->left && bin->left->resolvedType) {
            auto k = bin->left->resolvedType->kind;
            isFloat = (k == TypeKind::Float32 || k == TypeKind::Float64);
        }
        if (isFloat) {
            compileExpr(bin->left);
            emit("push rax");
            compileExpr(bin->right);
            emit("movq xmm1, rax");
            emit("pop rax");
            emit("movq xmm0, rax");
            switch (bin->op) {
                case Operand::Add: emit("addsd xmm0, xmm1"); break;
                case Operand::Sub: emit("subsd xmm0, xmm1"); break;
                case Operand::Mul: emit("mulsd xmm0, xmm1"); break;
                case Operand::Div: emit("divsd xmm0, xmm1"); break;
                case Operand::Less: case Operand::Greater:
                case Operand::LessEqual: case Operand::GreaterEqual:
                case Operand::EqualEqual: case Operand::NotEqual: {
                    emit("ucomisd xmm0, xmm1");
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
                    emit(std::string("set") + cc + " al");
                    emit("movzx rax, al");
                    return;
                }
                default: break;
            }
            emit("movq rax, xmm0");
            return;
        }

        //  Вычисляем операнды: левый → на стек, правый → rbx, левый → rax
        compileExpr(bin->left);
        emit("push rax");
        compileExpr(bin->right);
        emit("mov rbx, rax");
        emit("pop rax");

        //  Знаковость — по типу операнда (для сравнений bin->resolvedType всегда bool)
        bool isUnsigned = false;
        if (bin->left && bin->left->resolvedType) {
            auto k = bin->left->resolvedType->kind;
            isUnsigned = (k == TypeKind::Uint8  || k == TypeKind::Uint16
                       || k == TypeKind::Uint32 || k == TypeKind::Uint64);
        }

        switch (bin->op) {
            case Operand::Add: emit("add rax, rbx"); break;
            case Operand::Sub: emit("sub rax, rbx"); break;
            case Operand::Mul:
                if (isUnsigned) emit("mul rbx");           //  rdx:rax = rax * rbx
                else            emit("imul rax, rbx");
                break;
            case Operand::Div:
                if (isUnsigned) { emit("xor rdx, rdx"); emit("div rbx"); }
                else            { emit("cqo");          emit("idiv rbx"); }
                break;
            case Operand::Mod:
                if (isUnsigned) { emit("xor rdx, rdx"); emit("div rbx"); }
                else            { emit("cqo");          emit("idiv rbx"); }
                emit("mov rax, rdx");
                break;
            case Operand::Less: case Operand::Greater:
            case Operand::LessEqual: case Operand::GreaterEqual:
            case Operand::EqualEqual: case Operand::NotEqual: {
                emit("cmp rax, rbx");
                const char* cc = "e";
                switch (bin->op) {
                    case Operand::Less:         cc = isUnsigned ? "b"  : "l";  break;
                    case Operand::Greater:      cc = isUnsigned ? "a"  : "g";  break;
                    case Operand::LessEqual:    cc = isUnsigned ? "be" : "le"; break;
                    case Operand::GreaterEqual: cc = isUnsigned ? "ae" : "ge"; break;
                    case Operand::EqualEqual:   cc = "e";                      break;
                    case Operand::NotEqual:     cc = "ne";                     break;
                    default: break;
                }
                emit(std::string("set") + cc + " al");
                emit("movzx rax, al");
                break;
            }
            default: break;                                //  And/Or обработаны выше
        }
        return;
    }
    if (auto* fc = dynamic_cast<FuncCall*>(e)) {
        std::string callName;
        if (auto* id = dynamic_cast<Identifier*>(fc->callee)) callName = id->name;
        else if (auto* na = dynamic_cast<NamespaceAccess*>(fc->callee))
            callName = na->nameSpace + "__" + na->member;
        else return;                                 //  Методы экземпляра — через FieldAccess, позже

        //  Встроенные функции
        if (callName == "print" && fc->args.size() == 1) {
            compileExpr(fc->args[0]);
            emit("mov rdi, rax");
            auto t = fc->args[0]->resolvedType;
            if (t && t->kind == TypeKind::Bool)        emit("call print_bool");
            else if (t && t->kind == TypeKind::String) emit("call print_string");
            else                                       emit("call print_int");
            return;
        }
        if (callName == "len" && fc->args.size() == 1) {
            compileExpr(fc->args[0]);
            emit("mov rdi, rax");
            auto t = fc->args[0]->resolvedType;
            if (t && t->kind == TypeKind::String) {
                emit("call lang_strlen");
            } else if (t && t->kind == TypeKind::DynArray) {
                //  DynArray: { ptr, len, cap } — len по смещению +8
                emit("mov rax, [rdi+8]");
            } else if (t && t->kind == TypeKind::Array) {
                //  Статический массив — длина известна на этапе компиляции
                emit("mov rax, " + std::to_string(t->arraySize));
            }
            return;
        }

        //  Вычисляем аргументы в обратном порядке: первыми кладём «дальние»
        for (int i = (int)fc->args.size() - 1; i >= 0; --i) {
            compileExpr(fc->args[i]);
            emit("push rax");
        }
        //  Первые 6 из стека в регистры
        static const char* argRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int n = (int)fc->args.size();
        int inRegs = n < 6 ? n : 6;
        for (int i = 0; i < inRegs; ++i)
            emit("pop " + std::string(argRegs[i]));

        emit("call lang_" + callName);

        if (n > 6) emit("add rsp, " + std::to_string((n - 6) * 8));
        return;
    }
    //  String / ArrayLiteral / NewExpr / ... — в следующих фазах
}

//  ─── Финальная сборка ───

std::expected<void, std::string> CodeGen::finalize(const std::string& outPath) {
    std::string asmPath = outPath + ".asm";
    std::string objPath = outPath + ".o";

    //  Склеиваем секции в итоговый файл
    std::ofstream out(asmPath);
    if (!out)
        return std::unexpected("codegen: cannot open '" + asmPath + "' for writing");

    if (!data.str().empty())   out << "section .data\n"   << data.str()   << "\n";
    if (!rodata.str().empty()) out << "section .rodata\n" << rodata.str() << "\n";
    if (!bss.str().empty())    out << "section .bss\n"    << bss.str()    << "\n";
    out << "section .text\n" << text.str();
    out.close();

    //  nasm -f elf64
    std::string nasmCmd = "nasm -f elf64 " + asmPath + " -o " + objPath;
    if (std::system(nasmCmd.c_str()) != 0)
        return std::unexpected("codegen: nasm failed on '" + asmPath + "'");

    //  Линкуем с рантаймом runtime/runtime.o
    std::string ldCmd = "ld " + objPath + " runtime/runtime.o -o " + outPath;
    if (std::system(ldCmd.c_str()) != 0)
        return std::unexpected("codegen: ld failed");

    //  Чистим промежуточный объектник
    std::remove(objPath.c_str());
    return {};
}
