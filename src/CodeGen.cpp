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
        else if (c == '\n') rodata << "\\n";
        else if (c == '\r') rodata << "\\r";
        else if (c == '\t') rodata << "\\t";
        else if (c < ' ' || c > '~') rodata << "`, " << (int)c << ", `";
        else rodata << c;
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

//  Инициализирует слот глобалки в прологе main. Логика зеркалит локальный VarDecl,
//  но пишет в .bss-лейбл __global_<name> вместо стэковой локалки.
void CodeGen::compileGlobalInit(VarDecl* var) {
    bool declaredDyn = var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]";
    std::string slot = "__global_" + var->name;

    if (declaredDyn) {
        if (auto* arrLit = dynamic_cast<ArrayLiteral*>(var->init)) {    //  Литерал массива: аллоцируем буфер, пишем qword'ами
            int n = (int)arrLit->elements.size();
            text << "    mov rdi, " << (n * 8) << "\n";
            text << "    call lang_alloc\n";
            text << "    mov [rel " << slot << "], rax\n";                  //  ptr
            text << "    mov qword [rel " << slot << " + 8], " << n << "\n";   //  len
            text << "    mov qword [rel " << slot << " + 16], " << n << "\n"; //  cap
            for (int i = 0; i < n; ++i) {
                text << "    push qword [rel " << slot << "]\n";
                compileExpr(arrLit->elements[i]);
                text << "    pop rbx\n";
                text << "    mov [rbx + " << (i * 8) << "], rax\n";
            }
            return;
        }
        if (!var->init) {   //  Без инициализатора — пустой DynArray
            text << "    mov qword [rel " << slot << "], 0\n";
            text << "    mov qword [rel " << slot << " + 8], 0\n";
            text << "    mov qword [rel " << slot << " + 16], 0\n";
            return;
        }
    }
    if (var->init) {
        compileExpr(var->init);
        text << "    mov [rel " << slot << "], rax\n";
    }
}

//  Достаточный тип для выделения слота параметра: распознаём только DynArray (T[]).
//  Примитивы и struct/class → nullptr; для них sizeOfType(nullptr) == 8, что корректно.
std::shared_ptr<Type> CodeGen::typeFromName(const std::string& name) const {
    if (name.size() >= 2 && name.substr(name.size() - 2) == "[]") {
        auto t = std::make_shared<Type>();
        t->kind = TypeKind::DynArray;
        return t;
    }
    return nullptr;
}

//  Регистрирует смещение полей struct/class: скаляры/указатели — qword, DynArray (T[]) — 3 qword'а (ptr/len/cap)
void CodeGen::collectLayout(const std::string& name, const std::vector<StructField>& fields) {
    auto& layout = structLayouts[name];
    int offset = 0;
    for (auto& field : fields) {
        layout[field.name] = offset;
        bool isDynArray = (field.typeName.size() >= 2 && field.typeName.substr(field.typeName.size() - 2) == "[]");
        if (isDynArray) offset += 24;
        else offset += 8;
    }
    structSizes[name] = offset;
}

//  Обходит AST и регистрирует смещение для всех struct/class/namespace/export
void CodeGen::collectDecls(Stmt* decl) {
    if (!decl) return;
    if (auto* structDecl = dynamic_cast<StructDecl*>(decl)) {
        collectLayout(structDecl->name, structDecl->fields);
        if (!structDecls.count(structDecl->name)) {
            structDecls[structDecl->name] = structDecl;
            structDeclsOrdered.push_back(structDecl);
        }
    }
    else if (auto* classDecl = dynamic_cast<ClassDecl*>(decl)) {
        collectLayout(classDecl->name, classDecl->fields);
        if (!classDecls.count(classDecl->name)) {
            classDecls[classDecl->name] = classDecl;
            classDeclsOrdered.push_back(classDecl);
        }
    }
    else if (auto* exp = dynamic_cast<ExportDecl*>(decl)) {
        collectDecls(exp->decl);
    }
    else if (auto* ns  = dynamic_cast<NamespaceDecl*>(decl)) {
        for (auto* stmt : ns->decls) collectDecls(stmt);
    }
    else if (auto* var = dynamic_cast<VarDecl*>(decl)) {    //  Глобальная переменная: регистрируем имя и её тип
        if (!globals.count(var->name)) {
            std::shared_ptr<Type> type = nullptr;
            bool declaredDyn = var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]";
            if (declaredDyn) {
                auto arrType = std::make_shared<Type>();
                arrType->kind = TypeKind::DynArray;
                type = arrType;
            }
            else if (var->init) {
                type = var->init->resolvedType;
            }
            globals[var->name] = type;
            globalVars.push_back(var);
        }
    }
}

bool CodeGen::isGlobal(const std::string& name) const {
    return globals.count(name) > 0;
}

//  Вычисляет адрес DynArray-источника в регистр reg. Возвращает true при успехе.
//  Поддерживает: локальная DynArray; поле текущего класса; ClassName.field (дефолтный экземпляр); глобальная DynArray.
bool CodeGen::emitDynArrayAddr(Expr* e, const char* reg) {
    if (auto* id = dynamic_cast<Identifier*>(e)) {
        const LocalVar* lv = findLocal(id->name);
        if (lv) {
            text << "    lea " << reg << ", [rbp" << lv->offset << "]\n";
            return true;
        }
        if (currentClass) {
            auto& layout = structLayouts[currentClass->name];
            if (layout.count(id->name)) {
                const LocalVar* selfVar = findLocal("self");
                int off = layout[id->name];
                text << "    mov " << reg << ", [rbp" << selfVar->offset << "]\n";
                if (off != 0) text << "    add " << reg << ", " << off << "\n";
                return true;
            }
        }
        if (isGlobal(id->name)) {
            text << "    lea " << reg << ", [rel __global_" << id->name << "]\n";
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
                text << "    lea " << reg << ", [rel __default_instance_" << obj->name << " + " << off << "]\n";
                return true;
            }
        }
        //  instance.field — экземпляр (не имя класса): compileExpr кладёт ptr в rax,
        //  затем reg = rax + field_offset.
        if (fa->object && fa->object->resolvedType) {
            auto type = fa->object->resolvedType;
            auto it = structLayouts.find(type->name);
            if (it != structLayouts.end() && it->second.count(fa->field)) {
                int off = it->second[fa->field];
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

    if (auto* classDecl  = dynamic_cast<ClassDecl*>(decl)) {    //  Компилируем методы класса с self в rdi
        currentClass = classDecl;
        if (classDecl->constructor) {
            compileMethod(classDecl->constructor, classDecl->name + "_" + classDecl->name);
        }
        if (classDecl->destructor) {
            compileMethod(classDecl->destructor, classDecl->name + "_dtor");
        }
        for (auto* method : classDecl->methods) {
            compileMethod(method, classDecl->name + "_" + method->name);
        }
        currentClass = nullptr;
        return;
    }

    if (auto* ns = dynamic_cast<NamespaceDecl*>(decl)) {
        std::string prefix;
        if (classPrefix.empty()) prefix = ns->name;
        else                     prefix = classPrefix + "__" + ns->name;
        for (auto* stmt : ns->decls) compileDecl(stmt, prefix);    //  Функции из namespace помечаем префиксом чтобы различить
        return;
    }
    //  StructDecl / TypeAlias / ImportDecl / VarDecl на верхнем уровне — ничего не генерируют
    //  (глобалки инициализируются из пролога main через compileGlobalInit).
}

//  Точка входа
std::expected<void, std::string> CodeGen::generate(Program* program, const std::string& outPath) {
    text << "extern print_int\n";       //  Импорты рантайма
    text << "extern print_string\n";
    text << "extern print_bool\n";
    text << "extern print_char\n";
    text << "extern print_float\n";
    text << "extern print_space\n";
    text << "extern print_newline\n";
    text << "extern lang_input\n";      //  lang_ префикс — чтобы не конфликтовать с libc (strlen/free/exit/input)
    text << "extern lang_strlen\n";
    text << "extern lang_panic\n";
    text << "extern lang_exit\n";
    text << "extern lang_alloc\n";
    text << "extern lang_free\n";
    text << "extern lang_push\n";
    text << "extern lang_pop\n";
    text << "extern lang_strcat\n\n";

    //  Сообщения рантайма (формирует lang_panic как "runtime error: <msg> at line <N>")
    rodata << "__rt_div_zero: db \"division by zero\", 0\n";
    rodata << "__rt_bounds:   db \"array index out of bounds\", 0\n";

    //  Первый проход — ставим смещение в struct/class
    for (auto* decl : program->decls) {
        collectDecls(decl);
    }

    //  Выделяем .bss-слоты под default-значения полей структур: один qword на поле с default.
    //  Это позволяет переопределять default в рантайме (`StructName.field = value`).
    for (auto* sd : structDeclsOrdered) {
        for (auto& field : sd->fields) {
            if (field.defaultValue) {
                bss << "__default_" << sd->name << "_" << field.name << ": resq 1\n";
            }
        }
    }

    //  Для классов: .bss-слот под «дефолтный экземпляр» (хранит текущее значение полей,
    //  позволяет вызвать MyClass.method() и использовать MyClass.field = x).
    for (auto* cd : classDeclsOrdered) {
        int size = 0;
        if (structSizes.count(cd->name)) size = structSizes[cd->name];
        if (size <= 0) size = 8;
        bss << "__default_instance_" << cd->name << ": resb " << size << "\n";
    }

    //  .bss-слоты под глобальные переменные: DynArray — 24 байта (ptr/len/cap), остальное — 8.
    for (auto* var : globalVars) {
        int size = sizeOfType(globals[var->name]);
        if (size < 8) size = 8;
        bss << "__global_" << var->name << ": resb " << size << "\n";
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
    locals.clear();     //  Чистим текущие локалки прошлых функций
    classLocals.clear();
    currentFrameSize = 0;   //  Байт на функцию
    currentEndLabel = ".end_of_" + func->name;  //  Имя метки переводящей в конец, для return

    std::string symbol = func->name;
    text << "global " << symbol << "\n";    //  Определяем нашу функцию глобальной
    text << symbol << ":\n";    //  Метка для неё

    text << "    push rbp\n";   //  Начало функции
    text << "    mov rbp, rsp\n";

    if (isMethod) {     //  Метод: self — неявный первый параметр в rdi
        allocLocal("self", nullptr);
    }

    for (auto& param : func->params) {  //  Выделяем память для параметров (DynArray получит 24 байта)
        allocLocal(param.name, typeFromName(param.typeName));   //  Здесь каждый параметр будет получать 8 байтов на стэке для выравнивания
    }

    int paramBytes = currentFrameSize;
    int bodyBytes = countLocalsSize(func->body);
    int frameSize = ((paramBytes + bodyBytes + 15) / 16) * 16;  //  Новый размер фрейма функции

    if (frameSize > 0) {
        text << "    sub rsp, " << frameSize << "\n";   //  Выделяем
    }

    static const char* intArgRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    int intIdx = 0, xmmIdx = 0, stackIdx = 0;

    if (isMethod) {     //  Сохраняем self (rdi) в его локалку, остальные параметры сдвигаются на rsi+
        const LocalVar* selfVar = findLocal("self");
        text << "    mov [rbp" << selfVar->offset << "], rdi\n";
        intIdx = 1;
    }

    for (const auto& param : func->params) {
        const LocalVar* localVar = findLocal(param.name);

        bool isFloat = (param.typeName == "float32" || param.typeName == "float64" || param.typeName == "float");
        bool isDynArray = (param.typeName.size() >= 2 && param.typeName.substr(param.typeName.size() - 2) == "[]");

        if (isDynArray && intIdx + 3 <= 6) {    //  DynArray по значению: ptr/len/cap в 3 int-регистрах подряд
            text << "    mov [rbp" << localVar->offset         << "], " << intArgRegs[intIdx++] << "\n";
            text << "    mov [rbp" << (localVar->offset + 8)   << "], " << intArgRegs[intIdx++] << "\n";
            text << "    mov [rbp" << (localVar->offset + 16)  << "], " << intArgRegs[intIdx++] << "\n";
        }
        else if (isDynArray) {    //  Регистров не хватило — все три qword'а лежат в стеке вызова
            int stackOffset0 = 16 + stackIdx++ * 8;
            int stackOffset1 = 16 + stackIdx++ * 8;
            int stackOffset2 = 16 + stackIdx++ * 8;
            text << "    mov rax, [rbp+" << stackOffset0 << "]\n";
            text << "    mov [rbp" << localVar->offset << "], rax\n";
            text << "    mov rax, [rbp+" << stackOffset1 << "]\n";
            text << "    mov [rbp" << (localVar->offset + 8)  << "], rax\n";
            text << "    mov rax, [rbp+" << stackOffset2 << "]\n";
            text << "    mov [rbp" << (localVar->offset + 16) << "], rax\n";
        }
        else if (isFloat && xmmIdx < 8) {    //  float значение передаём через xmm регистры
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

    //  В начале main инициализируем default-слоты структур из их объявленных выражений.
    //  Делаем это до тела пользователя, чтобы любые struct-литералы видели уже заполненные значения.
    if (!isMethod && func->name == "main") {
        for (auto* sd : structDeclsOrdered) {
            for (auto& field : sd->fields) {
                if (!field.defaultValue) continue;
                compileExpr(field.defaultValue);
                text << "    mov [rel __default_" << sd->name << "_" << field.name << "], rax\n";
            }
        }
        //  И дефолтные экземпляры классов: для каждого поля с default кладём значение
        //  в слот экземпляра по его смещению.
        for (auto* cd : classDeclsOrdered) {
            auto& layout = structLayouts[cd->name];
            for (auto& field : cd->fields) {
                if (!field.defaultValue) continue;
                int off = 0;
                if (layout.count(field.name)) off = layout[field.name];
                compileExpr(field.defaultValue);
                text << "    mov [rel __default_instance_" << cd->name << " + " << off << "], rax\n";
            }
        }
        //  Инициализация глобальных переменных — после дефолтов, чтобы init мог ссылаться на StructName/ClassName.
        for (auto* var : globalVars) {
            compileGlobalInit(var);
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
            std::string lbl = newLabel("no_dtor");
            text << "    mov rdi, [rbp" << i->first << "]\n";
            text << "    test rdi, rdi\n";
            text << "    jz " << lbl << "\n";
            text << "    call " << i->second << "_dtor\n";
            text << lbl << ":\n";
        }
        text << "    mov rax, [rsp]\n";
        text << "    add rsp, 16\n";
    }
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
        bool declaredDyn = var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]";
        if (declaredDyn) {              //  `T[] x = ...` — слот всегда 24 байта (ptr/len/cap), независимо от init
            auto arrType = std::make_shared<Type>();
            arrType->kind = TypeKind::DynArray;
            type = arrType;
        }
        else if (var->init) {
            type = var->init->resolvedType; //  У обычной переменной берём тип
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
        bool declaredDyn = var->typeName.size() >= 2 && var->typeName.substr(var->typeName.size() - 2) == "[]";
        if (declaredDyn) {              //  Декларация `T[] x` — тип DynArray даже если init — Array-литерал
            auto arrType = std::make_shared<Type>();
            arrType->kind = TypeKind::DynArray;
            type = arrType;
        }
        else if (var->init) {
            type = var->init->resolvedType; //  Тип берём из init (семантик уже проставил resolvedType)
        }

        int off = allocLocal(var->name, type);
        //  Класс-типа локалку регистрируем для scope-exit дтор-вызова и зануляем слот,
        //  чтобы без-инициализатора/до-присваивания сравнение `test rdi, rdi` пропускало вызов.
        if (type && type->kind == TypeKind::Class && classDecls.count(type->name) && classDecls[type->name]->destructor) {
            text << "    mov qword [rbp" << off << "], 0\n";
            classLocals.push_back({off, type->name});
        }
        //  DynArray, инициализированный литералом массива: аллоцируем буфер, пишем qword'ами, заполняем ptr/len/cap.
        if (declaredDyn) {
            if (auto* arrLit = dynamic_cast<ArrayLiteral*>(var->init)) {
                int n = (int)arrLit->elements.size();
                text << "    mov rdi, " << (n * 8) << "\n";
                text << "    call lang_alloc\n";
                text << "    mov [rbp" << off << "], rax\n";                //  ptr
                text << "    mov qword [rbp" << (off + 8) << "], " << n << "\n";   //  len
                text << "    mov qword [rbp" << (off + 16) << "], " << n << "\n";  //  cap
                for (int i = 0; i < n; ++i) {
                    text << "    push qword [rbp" << off << "]\n";
                    compileExpr(arrLit->elements[i]);
                    text << "    pop rbx\n";
                    text << "    mov [rbx + " << (i * 8) << "], rax\n";
                }
                return;
            }
            
            if (!var->init) {
                text << "    mov qword [rbp" << off << "], 0\n";
                text << "    mov qword [rbp" << (off + 8) << "], 0\n";
                text << "    mov qword [rbp" << (off + 16) << "], 0\n";
            }
            return;
        }
        if (var->init) {
            compileExpr(var->init);
            text << "    mov [rbp" << off << "], rax\n";
        }
        return;
    }
    if (auto* assign = dynamic_cast<Assign*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assign->target)) {
            //  Присваивание полю текущего класса: [self + field_off] = value
            if (currentClass) {
                auto& layout = structLayouts[currentClass->name];
                if (layout.count(id->name)) {
                    const LocalVar* selfVar = findLocal("self");
                    int off = layout[id->name];
                    compileExpr(assign->value);
                    text << "    mov rbx, [rbp" << selfVar->offset << "]\n";
                    text << "    mov [rbx + " << off << "], rax\n";
                    return;
                }
            }
            const LocalVar* localVar = findLocal(id->name);
            if (localVar) {
                compileExpr(assign->value);
                text << "    mov [rbp" << localVar->offset << "], rax\n";   //  Закидываем в локалку по сдвигу
                return;
            }
            if (isGlobal(id->name)) {   //  Глобалка: пишем в .bss-слот
                compileExpr(assign->value);
                text << "    mov [rel __global_" << id->name << "], rax\n";
                return;
            }
            return;
        }
        if (auto* fieldAccess = dynamic_cast<FieldAccess*>(assign->target)) {
            //  Поле — DynArray: нужно писать 3 qword'а (ptr/len/cap), а не один.
            //  Исключение — StructName.field: слот `__default_<Struct>_<field>` всего 8 байт,
            //  корректно хранить туда DynArray нельзя, падаем на старое поведение (один qword).
            auto fieldType = fieldAccess->resolvedType;
            bool isDynArr = fieldType && fieldType->kind == TypeKind::DynArray;
            bool structDefault = false;
            if (auto* id = dynamic_cast<Identifier*>(fieldAccess->object)) {
                if (structDecls.count(id->name)) structDefault = true;
            }
            if (isDynArr && !structDefault) {
                if (auto* arrLit = dynamic_cast<ArrayLiteral*>(assign->value)) {
                    int n = (int)arrLit->elements.size();
                    emitDynArrayAddr(fieldAccess, "rdi");        //  rdi = адрес 24-байтового слота
                    text << "    push rdi\n";
                    text << "    mov rdi, " << (n * 8) << "\n";
                    text << "    call lang_alloc\n";
                    text << "    pop rbx\n";
                    text << "    mov [rbx], rax\n";
                    text << "    mov qword [rbx + 8], " << n << "\n";
                    text << "    mov qword [rbx + 16], " << n << "\n";
                    for (int i = 0; i < n; ++i) {
                        text << "    push rbx\n";
                        compileExpr(arrLit->elements[i]);
                        text << "    pop rbx\n";
                        text << "    mov rcx, [rbx]\n";
                        text << "    mov [rcx + " << (i * 8) << "], rax\n";
                    }
                    return;
                }
                //  Иначе — источник должен быть DynArray-местом (локалка / поле / глобалка):
                //  копируем 24 байта из источника в приёмник.
                if (emitDynArrayAddr(assign->value, "rsi")) {
                    text << "    push rsi\n";
                    emitDynArrayAddr(fieldAccess, "rdi");
                    text << "    pop rsi\n";
                    text << "    mov rax, [rsi]\n";
                    text << "    mov [rdi], rax\n";
                    text << "    mov rax, [rsi + 8]\n";
                    text << "    mov [rdi + 8], rax\n";
                    text << "    mov rax, [rsi + 16]\n";
                    text << "    mov [rdi + 16], rax\n";
                    return;
                }
                //  Источник не опознан — падаем на старое поведение ниже.
            }

            //  Переопределение default-значения структуры: `Point.x = 4;`.
            //  Если object — это идентификатор, совпадающий с именем структуры, пишем в слот.
            if (auto* id = dynamic_cast<Identifier*>(fieldAccess->object)) {
                if (structDecls.count(id->name)) {
                    compileExpr(assign->value);
                    text << "    mov [rel __default_" << id->name << "_" << fieldAccess->field << "], rax\n";
                    return;
                }
            }
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

            std::shared_ptr<Type> objType = nullptr;
            if (aa->object) objType = aa->object->resolvedType;
            int sz = 8;
            if (objType && objType->kind == TypeKind::Array) {
                sz = sizeOfType(objType->elementType);
            }
            //  DynArray/String/прочее — qword; String как l-value всё равно семантикой запрещён
            if (sz == 1)      text << "    mov byte [rcx + rbx], al\n";
            else if (sz == 2) text << "    mov word [rcx + rbx*2], ax\n";
            else if (sz == 4) text << "    mov dword [rcx + rbx*4], eax\n";
            else              text << "    mov [rcx + rbx*8], rax\n";
            return;
        }
        return;
    }
    if (auto* ds = dynamic_cast<DeleteStmt*>(stmt)) {
        //  Если удаляемый указатель — класс с деструктором, зовём <Class>_dtor до lang_free.
        std::string dtorClass;
        auto t = ds->value ? ds->value->resolvedType : nullptr;
        if (t && t->kind == TypeKind::Class && classDecls.count(t->name) && classDecls[t->name]->destructor) {
            dtorClass = t->name;
        }
        compileExpr(ds->value);
        if (!dtorClass.empty()) {
            //  Сохраняем ptr в aligned-слоте: sub rsp,16; mov [rsp], rax — rsp%16==0 перед call.
            text << "    sub rsp, 16\n";
            text << "    mov [rsp], rax\n";
            text << "    mov rdi, rax\n";
            text << "    call " << dtorClass << "_dtor\n";
            text << "    mov rdi, [rsp]\n";
            text << "    add rsp, 16\n";
        }
        else {
            text << "    mov rdi, rax\n";
        }
        text << "    call lang_free\n";
        //  Обнуляем исходную переменную, чтобы scope-exit дтор не сработал повторно.
        if (auto* id = dynamic_cast<Identifier*>(ds->value)) {
            const LocalVar* lv = findLocal(id->name);
            if (lv) {
                text << "    mov qword [rbp" << lv->offset << "], 0\n";
            }
            else if (isGlobal(id->name)) {
                text << "    mov qword [rel __global_" << id->name << "], 0\n";
            }
        }
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
    if (auto* str = dynamic_cast<String*>(expr)) {  //  Строка
        //  Если семантик сузил литерал до Char ("!" в `char c = "!";`) — загружаем сам байт, а не адрес.
        if (expr->resolvedType && expr->resolvedType->kind == TypeKind::Char && str->value.size() == 1) {
            text << "    mov rax, " << (int)(unsigned char)str->value[0] << "\n";
            return;
        }
        std::string label = internString(str->value);   //  пишем строку в rodata
        text << "    lea rax, [rel " << label << "]\n"; //  берём адрес строки по её метке
        return;
    }
    if (auto* arrayLit = dynamic_cast<ArrayLiteral*>(expr)) {   //  Литерал массива
        std::string label = "arr" + std::to_string(arrayCounter++);     //  Метка в data

        //  Директива данных по размеру элемента: db/dw/dd/dq
        std::shared_ptr<Type> elemT;
        if (expr->resolvedType) elemT = expr->resolvedType->elementType;
        int elemSz = sizeOfType(elemT);
        const char* directive = "dq";
        if (elemSz == 1)      directive = "db";
        else if (elemSz == 2) directive = "dw";
        else if (elemSz == 4) directive = "dd";

        data << label << ": " << directive << " ";

        for (size_t i = 0; i < arrayLit->elements.size(); ++i) {
            auto* elem = arrayLit->elements[i];
            if (auto* num = dynamic_cast<Number*>(elem)) {
                //  Решаем по целевому типу элемента массива, а не по синтаксису литерала:
                //  в `float[3] = [3.14, 5, 6.2]` для `5` нужно записать биты 5.0.
                bool asFloat = false;
                if (elemT && (elemT->kind == TypeKind::Float32 || elemT->kind == TypeKind::Float64))
                    asFloat = true;
                if (asFloat) {
                    if (elemSz == 4) {
                        float value = (float)num->value;
                        uint32_t bits;
                        std::memcpy(&bits, &value, sizeof(bits));    //  float32 raw-биты
                        data << bits;
                    }
                    else {
                        double value = num->value;
                        uint64_t bits;
                        std::memcpy(&bits, &value, sizeof(bits));    //  float64 raw-биты
                        data << bits;
                    }
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
                    bool asFloat = false;
                    if (elemT && (elemT->kind == TypeKind::Float32 || elemT->kind == TypeKind::Float64))
                        asFloat = true;
                    if (asFloat) {
                        if (elemSz == 4) {
                            float value = -(float)num->value;
                            uint32_t bits;
                            std::memcpy(&bits, &value, sizeof(bits));
                            data << bits;
                        }
                        else {
                            double value = -num->value;
                            uint64_t bits;
                            std::memcpy(&bits, &value, sizeof(bits));
                            data << bits;
                        }
                    } else {
                        data << -(long long)num->value;
                    }
                } else {
                    data << "0";                            
                }
            }
            else {
                data << "0";                                 
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
        //  Обнуляем весь объект: для DynArray-поля это гарантирует корректные len/cap=0,
        //  если поле не задано явно (lang_alloc может вернуть неинициализированные байты
        //  при повторном использовании адресов).
        {
            int zq = size / 8;
            int zt = size % 8;
            for (int k = 0; k < zq; k++) {
                text << "    mov qword [rax + " << (k * 8) << "], 0\n";
            }
            for (int k = 0; k < zt; k++) {
                text << "    mov byte [rax + " << (zq * 8 + k) << "], 0\n";
            }
        }
        auto& layout = structLayouts[sl->name];

        //  Собираем имена полей, для которых значение задано явно
        std::unordered_set<std::string> explicitFields;
        for (auto& fi : sl->fields) {
            explicitFields.insert(fi.name);
        }

        //  Сначала — явно указанные поля литерала
        for (auto& fi : sl->fields) {
            compileExpr(fi.value);
            text << "    mov rbx, [rsp]\n";                  //  базовый адрес объекта
            int off = 0;
            if (layout.count(fi.name)) {
                off = layout[fi.name];
            }
            text << "    mov [rbx+" << off << "], rax\n";
        }

        //  Затем — default-значения для опущенных полей. Берём из runtime-слота,
        //  чтобы учитывались все переопределения вида `StructName.field = value;`.
        if (structDecls.count(sl->name)) {
            StructDecl* sd = structDecls[sl->name];
            for (auto& field : sd->fields) {
                if (explicitFields.count(field.name)) continue;     //  Пользователь задал явно
                if (!field.defaultValue) continue;                   //  Нет значения по умолчанию
                text << "    mov rax, [rel __default_" << sl->name << "_" << field.name << "]\n";
                text << "    mov rbx, [rsp]\n";
                int off = 0;
                if (layout.count(field.name)) {
                    off = layout[field.name];
                }
                text << "    mov [rbx+" << off << "], rax\n";
            }
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
        //  new ClassName(args) — alloc + (опционально) вызов конструктора с this в rdi
        int size = 8;
        if (structSizes.count(newExpr->className)) {
            size = structSizes[newExpr->className];
        }
        text << "    mov rdi, " << size << "\n";
        text << "    call lang_alloc\n";
        text << "    push rax\n";                            //  this

        //  Копируем байты дефолтного экземпляра класса в свежевыделенный объект,
        //  чтобы default-значения полей и переопределения (MyClass.field = ...) применились.
        if (classDecls.count(newExpr->className) && size > 0) {
            int qwords = size / 8;
            int tail   = size % 8;
            for (int k = 0; k < qwords; k++) {
                text << "    mov rax, [rel __default_instance_" << newExpr->className << " + " << (k * 8) << "]\n";
                text << "    mov rbx, [rsp]\n";
                text << "    mov [rbx + " << (k * 8) << "], rax\n";
            }
            for (int k = 0; k < tail; k++) {
                text << "    mov al, [rel __default_instance_" << newExpr->className << " + " << (qwords * 8 + k) << "]\n";
                text << "    mov rbx, [rsp]\n";
                text << "    mov [rbx + " << (qwords * 8 + k) << "], al\n";
            }
        }

        //  Поля с default вида `T = Struct{...}` дают heap-указатель, одинаковый во всех
        //  инстансах при bytewise-копии. Переаллоцируем свежую копию на каждый `new`.
        //  DynArray-поля — отдельный случай: 24-байтовый слот (ptr/len/cap) в дефолтном
        //  экземпляре шарится буфером, поэтому на каждый `new` инициализируем поле заново
        //  (из ArrayLiteral-дефолта либо пустым), независимо от содержимого дефолт-инстанса.
        if (classDecls.count(newExpr->className)) {
            ClassDecl* cd = classDecls[newExpr->className];
            auto& layout = structLayouts[cd->name];
            for (auto& field : cd->fields) {
                bool isDynArr = (field.typeName.size() >= 2
                              && field.typeName.substr(field.typeName.size() - 2) == "[]");
                int off = 0;
                if (layout.count(field.name)) off = layout[field.name];
                if (isDynArr) {
                    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(field.defaultValue)) {
                        int n = (int)arrLit->elements.size();
                        text << "    mov rdi, " << (n * 8) << "\n";
                        text << "    call lang_alloc\n";
                        text << "    mov rbx, [rsp]\n";
                        text << "    mov [rbx + " << off << "], rax\n";
                        text << "    mov qword [rbx + " << (off + 8)  << "], " << n << "\n";
                        text << "    mov qword [rbx + " << (off + 16) << "], " << n << "\n";
                        for (int i = 0; i < n; ++i) {
                            text << "    mov rbx, [rsp]\n";
                            text << "    push qword [rbx + " << off << "]\n";
                            compileExpr(arrLit->elements[i]);
                            text << "    pop rbx\n";
                            text << "    mov [rbx + " << (i * 8) << "], rax\n";
                        }
                    }
                    else {
                        text << "    mov rbx, [rsp]\n";
                        text << "    mov qword [rbx + " << off       << "], 0\n";
                        text << "    mov qword [rbx + " << (off + 8) << "], 0\n";
                        text << "    mov qword [rbx + " << (off + 16)<< "], 0\n";
                    }
                    continue;
                }
                if (!field.defaultValue) continue;
                if (!dynamic_cast<StructLiteral*>(field.defaultValue)) continue;
                compileExpr(field.defaultValue);
                text << "    mov rbx, [rsp]\n";
                text << "    mov [rbx + " << off << "], rax\n";
            }
        }

        bool hasCtor = classDecls.count(newExpr->className) && classDecls[newExpr->className]->constructor;
        if (hasCtor) {
            //  Аргументы: пока поддерживаем 0–5 (this занимает rdi)
            for (int i = (int)newExpr->args.size() - 1; i >= 0; --i) {
                compileExpr(newExpr->args[i]);
                text << "    push rax\n";
            }
            static const char* argRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};

            for (size_t i = 0; i < newExpr->args.size() && i < 5; ++i) {
                text << "    pop " << argRegs[i] << "\n";
            }
            text << "    mov rdi, [rsp]\n";                  //  this снова в rdi
            text << "    call " << newExpr->className << "_" << newExpr->className << "\n";
        }
        text << "    pop rax\n";                             //  результат new — указатель
        return;
    }
    if (auto* aa = dynamic_cast<ArrayAccess*>(expr)) {
        std::shared_ptr<Type> objType = nullptr;
        if (aa->object) objType = aa->object->resolvedType;

        //  DynArray-путь с bounds-check: получаем &struct через emitDynArrayAddr (если доступно)
        if (objType && objType->kind == TypeKind::DynArray && emitDynArrayAddr(aa->object, "rax")) {
            text << "    push rax\n";                 //  сохраняем &struct
            compileExpr(aa->index);
            text << "    mov rbx, rax\n";
            text << "    pop rax\n";                  //  rax = &struct
            std::string okLabel = newLabel("bndok");
            text << "    mov rcx, [rax + 8]\n";       //  rcx = len
            text << "    cmp rbx, rcx\n";
            text << "    jb " << okLabel << "\n";
            text << "    lea rdi, [rel __rt_bounds]\n";
            text << "    mov rsi, " << aa->line << "\n";
            text << "    call lang_panic\n";
            text << okLabel << ":\n";
            text << "    mov rax, [rax]\n";           //  rax = ptr
            text << "    mov rax, [rax + rbx*8]\n";
            return;
        }

        //  Базовый адрес → push, индекс → rbx, pop base
        compileExpr(aa->object);
        text << "    push rax\n";
        compileExpr(aa->index);
        text << "    mov rbx, rax\n";
        text << "    pop rax\n";

        if (objType && objType->kind == TypeKind::String) {
            //  Строка: элемент — один байт (char), без масштаба
            text << "    movzx rax, byte [rax + rbx]\n";
        }
        else if (objType && objType->kind == TypeKind::DynArray) {
            //  DynArray (fallback, без bounds-check): адрес струкутуры восстановить не смогли
            text << "    mov rax, [rax + rbx*8]\n";
        }
        else if (objType && objType->kind == TypeKind::Array) {
            auto elemT = objType->elementType;
            int sz = sizeOfType(elemT);
            bool isSigned = elemT && (elemT->kind == TypeKind::Int8 || elemT->kind == TypeKind::Int16
                                   || elemT->kind == TypeKind::Int32 || elemT->kind == TypeKind::Int64);
            //  Bounds-check для статического массива: размер известен на компиляции
            {
                std::string okLabel = newLabel("bndok");
                text << "    cmp rbx, " << objType->arraySize << "\n";
                text << "    jb " << okLabel << "\n";
                text << "    lea rdi, [rel __rt_bounds]\n";
                text << "    mov rsi, " << aa->line << "\n";
                text << "    call lang_panic\n";
                text << okLabel << ":\n";
            }
            if (sz == 1) {
                if (isSigned) text << "    movsx rax, byte [rax + rbx]\n";
                else          text << "    movzx rax, byte [rax + rbx]\n";
            }
            else if (sz == 2) {
                if (isSigned) text << "    movsx rax, word [rax + rbx*2]\n";
                else          text << "    movzx rax, word [rax + rbx*2]\n";
            }
            else if (sz == 4) {
                if (isSigned) text << "    movsxd rax, dword [rax + rbx*4]\n";
                else          text << "    mov eax, dword [rax + rbx*4]\n";   //  zero-extend до rax
            }
            else {
                text << "    mov rax, [rax + rbx*8]\n";
            }
        }
        else {
            text << "    mov rax, [rax + rbx*8]\n";
        }
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
        //  Сокращение: бареИмяСтруктуры = пустой struct-литерал `Struct {}`.
        //  Аллоцируем объект и заливаем default-значения из слотов (поля без default остаются нулями от lang_alloc).
        if (structDecls.count(id->name)) {
            int size;
            if (structSizes.count(id->name)) size = structSizes[id->name];
            else                              size = (int)structDecls[id->name]->fields.size() * 8;
            text << "    mov rdi, " << size << "\n";
            text << "    call lang_alloc\n";
            text << "    push rax\n";
            auto& layout = structLayouts[id->name];
            for (auto& field : structDecls[id->name]->fields) {
                if (!field.defaultValue) continue;
                text << "    mov rax, [rel __default_" << id->name << "_" << field.name << "]\n";
                text << "    mov rbx, [rsp]\n";
                int off = 0;
                if (layout.count(field.name)) off = layout[field.name];
                text << "    mov [rbx+" << off << "], rax\n";
            }
            text << "    pop rax\n";
            return;
        }
        //  Имя класса — возвращаем адрес дефолтного экземпляра (для MyClass.method()/MyClass.field).
        if (classDecls.count(id->name)) {
            text << "    lea rax, [rel __default_instance_" << id->name << "]\n";
            return;
        }
        //  Внутри метода имя может быть полем текущего класса — читаем через self (rbp+self_off) + field_off.
        if (currentClass) {
            auto& layout = structLayouts[currentClass->name];
            if (layout.count(id->name)) {
                const LocalVar* selfVar = findLocal("self");
                int off = layout[id->name];
                text << "    mov rax, [rbp" << selfVar->offset << "]\n";
                text << "    mov rax, [rax + " << off << "]\n";
                return;
            }
        }
        const LocalVar* lv = findLocal(id->name);
        if (lv) {
            text << "    mov rax, [rbp" << lv->offset << "]\n";
            return;
        }
        if (isGlobal(id->name)) {
            text << "    mov rax, [rel __global_" << id->name << "]\n";
            return;
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
            if (unary->operand) t = unary->operand->resolvedType;
            bool isFloat = t && (t->kind == TypeKind::Float32 || t->kind == TypeKind::Float64);
            if (isFloat) {
                //  Для float — инвертируем только знаковый бит, не все биты
                text << "    mov rbx, 0x8000000000000000\n";
                text << "    xor rax, rbx\n";
            }
            else {
                text << "    neg rax\n";
            }
            return;
        }
        if (unary->op == Operand::Increment || unary->op == Operand::Decrement) {
            //  ++/-- на идентификаторе: читаем → меняем в памяти → возвращаем новое значение
            if (auto* id = dynamic_cast<Identifier*>(unary->operand)) {
                const LocalVar* lv = findLocal(id->name);
                if (lv) {
                    if (unary->op == Operand::Increment)
                        text << "    inc qword [rbp" << lv->offset << "]\n";
                    else
                        text << "    dec qword [rbp" << lv->offset << "]\n";
                    text << "    mov rax, [rbp" << lv->offset << "]\n";
                }
                else if (isGlobal(id->name)) {
                    std::string slot = "__global_" + id->name;
                    if (unary->op == Operand::Increment)
                        text << "    inc qword [rel " << slot << "]\n";
                    else
                        text << "    dec qword [rel " << slot << "]\n";
                    text << "    mov rax, [rel " << slot << "]\n";
                }
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

        //  Конкатенация строк: left + right → lang_strcat(left, right).
        //  Операнд-char превращаем в указатель на временную 2-байтовую строку "{c,\0}" на стеке.
        bool leftIsStr  = bin->left  && bin->left->resolvedType  && bin->left->resolvedType->kind  == TypeKind::String;
        bool rightIsStr = bin->right && bin->right->resolvedType && bin->right->resolvedType->kind == TypeKind::String;
        bool leftIsChar  = bin->left  && bin->left->resolvedType  && bin->left->resolvedType->kind  == TypeKind::Char;
        bool rightIsChar = bin->right && bin->right->resolvedType && bin->right->resolvedType->kind == TypeKind::Char;
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
                        case Operand::Less: cc = "b";  break;
                        case Operand::Greater: cc = "a";  break;
                        case Operand::LessEqual: cc = "be"; break;
                        case Operand::GreaterEqual: cc = "ae"; break;
                        case Operand::EqualEqual: cc = "e";  break;
                        case Operand::NotEqual: cc = "ne"; break;
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
        //  Метод экземпляра: obj.method(args) — self передаётся в rdi
        if (auto* fa = dynamic_cast<FieldAccess*>(fc->callee)) {
            std::shared_ptr<Type> objType;
            if (fa->object) objType = fa->object->resolvedType;
            if (!objType || objType->kind != TypeKind::Class) return;
            compileExpr(fa->object);
            text << "    push rax\n";                //  сохраним this
            for (int i = (int)fc->args.size() - 1; i >= 0; --i) {
                compileExpr(fc->args[i]);
                text << "    push rax\n";
            }
            static const char* methodArgRegs[5] = {"rsi", "rdx", "rcx", "r8", "r9"};
            size_t regArgs = std::min(fc->args.size(), (size_t)5);
            for (size_t i = 0; i < regArgs; ++i) {
                text << "    pop " << methodArgRegs[i] << "\n";
            }
            text << "    pop rdi\n";                 //  self
            text << "    call " << objType->name << "_" << fa->field << "\n";
            size_t stackArgs = fc->args.size() - regArgs;
            if (stackArgs > 0) text << "    add rsp, " << (stackArgs * 8) << "\n";
            return;
        }
        std::string callName;
        if (auto* id = dynamic_cast<Identifier*>(fc->callee)) callName = id->name;
        else if (auto* na = dynamic_cast<NamespaceAccess*>(fc->callee))
            callName = na->nameSpace + "_" + na->member;
        else return;

        //  Встроенные функции
        //  print(x1, x2, ...) — печатает аргументы через пробел, в конце \n.
        //  Массивы (Array/DynArray): элементы печатаются как int через пробел.
        if (callName == "print" && !fc->args.empty()) {
            for (size_t i = 0; i < fc->args.size(); i++) {
                auto argType = fc->args[i]->resolvedType;
                bool isArray    = argType && argType->kind == TypeKind::Array;
                bool isDynArray = argType && argType->kind == TypeKind::DynArray;

                if (isArray || isDynArray) {
                    //  rbx = базовый указатель, r12 = индекс, r13 = длина (все callee-saved → живут через call'ы)
                    int lbl = labelCounter++;
                    if (isDynArray) {
                        auto* id = dynamic_cast<Identifier*>(fc->args[i]);
                        if (!id) return;
                        const LocalVar* lv = findLocal(id->name);
                        if (!lv) return;
                        text << "    mov rbx, [rbp" << lv->offset       << "]\n";    //  ptr
                        text << "    mov r13, [rbp" << (lv->offset + 8) << "]\n";    //  len
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
                    //  DynArray внутри хранит qword'ы (lang_push); Array — упакованные по sizeOfType
                    int elemSz = 8;
                    if (isArray) elemSz = sizeOfType(elemType);
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
                        text << "    movq xmm0, rax\n";                //  compileExpr для float кладёт биты в rax
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
        //  push(arr, elem) — arr поддерживается как локальная DynArray или поле класса
        if (callName == "push" && fc->args.size() == 2) {
            //  Вычислим элемент первым (может клобберить rdi), сохраним на стеке
            compileExpr(fc->args[1]);
            text << "    push rax\n";
            if (!emitDynArrayAddr(fc->args[0], "rdi")) return;
            text << "    pop rsi\n";                             //  rsi = элемент
            text << "    call lang_push\n";
            return;
        }
        //  pop(arr) → rax = последний элемент
        if (callName == "pop" && fc->args.size() == 1) {
            if (!emitDynArrayAddr(fc->args[0], "rdi")) return;
            text << "    mov rsi, " << fc->line << "\n";
            text << "    call lang_pop\n";
            return;
        }
        if (callName == "len" && fc->args.size() == 1) {
            auto argType = fc->args[0]->resolvedType;
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

        int argCount = (int)fc->args.size();
        int qwordsPushed = 0;    //  DynArray-аргумент занимает 3 qword'а, обычный — 1
        for (int i = argCount - 1; i >= 0; --i) {
            auto& argType = fc->args[i]->resolvedType;
            bool isDyn = argType && argType->kind == TypeKind::DynArray;
            if (isDyn) {
                //  DynArray по значению: источник — Identifier локалки/глобалки (иначе не взять все 3 поля)
                auto* id = dynamic_cast<Identifier*>(fc->args[i]);
                if (!id) return;
                const LocalVar* lv = findLocal(id->name);
                if (lv) {
                    text << "    push qword [rbp" << (lv->offset + 16) << "]\n";  //  cap — ниже в стэке
                    text << "    push qword [rbp" << (lv->offset + 8)  << "]\n";  //  len — в середине
                    text << "    push qword [rbp" << lv->offset        << "]\n";  //  ptr — на вершине
                }
                else if (isGlobal(id->name)) {
                    std::string slot = "__global_" + id->name;
                    text << "    push qword [rel " << slot << " + 16]\n";
                    text << "    push qword [rel " << slot << " + 8]\n";
                    text << "    push qword [rel " << slot << "]\n";
                }
                else return;
                qwordsPushed += 3;
            }
            else {
                compileExpr(fc->args[i]);
                text << "    push rax\n";
                qwordsPushed += 1;
            }
        }

        static const char* intRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int intIdx = 0, xmmIdx = 0;
        int qwordsConsumed = 0;    //  Сколько qword'ов мы сняли со стэка в регистры
        for (int i = 0; i < argCount; ++i) {
            auto& argType = fc->args[i]->resolvedType;
            bool isDyn = argType && argType->kind == TypeKind::DynArray;
            bool isFloat = argType && (argType->kind == TypeKind::Float32 || argType->kind == TypeKind::Float64);
            if (isDyn && intIdx + 3 <= 6) {    //  DynArray занимает 3 int-регистра подряд
                text << "    pop " << intRegs[intIdx++] << "\n";    //  ptr
                text << "    pop " << intRegs[intIdx++] << "\n";    //  len
                text << "    pop " << intRegs[intIdx++] << "\n";    //  cap
                qwordsConsumed += 3;
            }
            else if (isDyn) {
                //  Регистров не осталось — DynArray-арг в стэк передавать не поддерживаем
                //  (каллер ABI стыкуется с компиляцией параметра, но требует правильной раскладки).
                //  Не ломаем кодген, оставляем 3 qword'а в стэке; dispatch последующих int-арг собьётся.
            }
            else if (isFloat && xmmIdx < 8) {
                text << "    movsd xmm" << xmmIdx++ << ", [rsp]\n";
                text << "    add rsp, 8\n";
                qwordsConsumed += 1;
            }
            else if (!isFloat && intIdx < 6) {
                text << "    pop " << intRegs[intIdx++] << "\n";
                qwordsConsumed += 1;
            }
        }
        int stackArgCount = qwordsPushed - qwordsConsumed;

        if (fc->isExternC) {
            externCFunctions.insert(callName);
            if (stackArgCount % 2 != 0) text << "    sub rsp, 8\n";
            if (fc->isVariadic) text << "    mov al, " << xmmIdx << "\n";
            text << "    call " << callName << "\n";
            if (stackArgCount % 2 != 0) text << "    add rsp, 8\n";
            if (stackArgCount > 0)      text << "    add rsp, " << (stackArgCount * 8) << "\n";
        } else {
            //  input/exit/panic живут в рантайме под lang_-именами (чтобы не конфликтовать с libc).
            //  Всё остальное — пользовательские/namespace/метод-функции без префикса.
            bool isRuntimeBuiltin = (callName == "input" || callName == "exit" || callName == "panic");
            if (isRuntimeBuiltin) text << "    call lang_" << callName << "\n";
            else                  text << "    call " << callName << "\n";
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
