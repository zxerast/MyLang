#pragma once

#include "Ast.hpp"
#include "Type.hpp"
#include <expected>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct LocalVar {
    int offset;                     //  Отрицательное смещение от rbp
    std::shared_ptr<Type> type;     //  Тип локальной переменной
};

struct LoopLabels {
    std::string breakLabel;     //  break
    std::string continueLabel;  //  continue
};

struct GlobalVarInit {
    VarDecl* decl;
    VarInit* var;
};

class CodeGen {     //  Буферы секций — собираем в них инструкции, склеиваем в finalize()
    std::ostringstream data;        //  инициализированные глобальные переменные
    std::ostringstream rodata;      //  строковые и неизменяемые литералы
    std::ostringstream bss;         //  неинициализированные глобальные переменные
    std::ostringstream text;        //  код

    int labelCounter = 0;           //  Счётчик для уникальных меток .L0, .L1, ...
    int stringCounter = 0;          //  Счётчик строковых литералов .str0, .str1, ...
    int arrayCounter  = 0;          //  Счётчик литералов массивов
    int structCounter = 0;          //  Счётчик временных слотов для структур/классов

    bool currentHasSRet = false;
    int currentSRetOffset = 0;

    std::unordered_map<std::shared_ptr<Symbol>, LocalVar> localsBySymbol;
    std::unordered_map<std::shared_ptr<Symbol>, std::shared_ptr<Type>> globalsBySymbol;
    std::unordered_map<std::shared_ptr<Symbol>, std::string> globalLabelsBySymbol;
    LocalVar selfLocal{0, nullptr};
    bool hasSelfLocal = false;

    int currentFrameSize = 0;       //  Сколько байт выделено в текущем фрейме
    std::string currentEndLabel; //  Метка конца функции для return
    std::vector<LoopLabels> loopStack;  //  Метки break и continue

    std::shared_ptr<Type> currentReturnType = nullptr;

    
    int storageSizeOfType(const std::shared_ptr<Type>& type) const;

    static bool isSignedIntType(const std::shared_ptr<Type>& type);
    static bool isUnsignedIntType(const std::shared_ptr<Type>& type);
    static bool isIntegerLikeType(const std::shared_ptr<Type>& type);
    static bool isFloatType(const std::shared_ptr<Type>& type);
    static bool isCompositeMemoryType(const std::shared_ptr<Type>& type);

    void emitNullCheck(const std::string& reg, int line);
    void emitNormalizeRax(const std::shared_ptr<Type>& type);
    void emitCastFromTo(const std::shared_ptr<Type>& from, const std::shared_ptr<Type>& to);

    // lvalue -> rax = адрес
    void emitAddress(Expr* expr);

    // addr — готовый memory operand: "[rbp-8]", "[rbx]", "[rel __global_x]"
    void emitLoad(const std::string& addr, const std::shared_ptr<Type>& type);

    // rax -> addr
    void emitStore(const std::string& addr, const std::shared_ptr<Type>& type);

    // dstReg/srcReg — регистры, в которых лежат адреса блоков памяти
    void emitCopy(const std::string& dstReg, const std::string& srcReg, const std::shared_ptr<Type>& type);

    // dstReg — регистр, в котором лежит адрес объекта
    void emitDefault(const std::string& dstReg, const std::shared_ptr<Type>& type);
    void emitDefaultAt(const std::string& dstReg, int offset, const std::shared_ptr<Type>& type);

    // expr -> rax, приведённый к targetType
    void compileExprAs(Expr* expr, const std::shared_ptr<Type>& targetType);

    //  Классовые локалки текущей функции — порядок объявления, для scope-exit вызова деструкторов.
    //  Пара (offset, className). Заполняется в compileStmt(VarDecl), очищается в compileFunction.
    std::vector<std::pair<int, std::string>> classLocals;

    std::unordered_map<std::string, std::string> stringPool;    //  Пул строковых литералов: текст → метка

    std::unordered_set<std::string> externCFunctions;   //  Имена C-функций, которые нужно объявить как extern

    std::unordered_map<std::string, std::unordered_map<std::string, int>> structLayouts;    //  Смещение структур/классов: {struct_name -> {field_name -> offset}}. Поле = qword.
    std::unordered_map<std::string, int> structSizes;   //  Суммарный размер типа структуры/класса в байтах
    
    std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<Type>>> structFieldTypes;
    std::unordered_map<std::string, std::vector<std::string>> structFieldOrder;
    
    std::unordered_map<std::string, StructDecl*> structDecls;   //  Имя структуры -> AST-узел (нужен для default-значений полей)
    std::vector<StructDecl*> structDeclsOrdered;                 //  Структуры в порядке объявления — для детерминированной инициализации default-слотов
    std::unordered_map<StructDecl*, std::string> structDeclNames;
    std::unordered_map<std::string, ClassDecl*> classDecls;      //  Имя класса -> AST-узел
    std::vector<ClassDecl*> classDeclsOrdered;                    //  Классы в порядке объявления — для детерминированной инициализации
    std::unordered_map<ClassDecl*, std::string> classDeclNames;
    ClassDecl* currentClass = nullptr;                            //  Класс, чей метод сейчас компилируется (для доступа к self-полям)
    std::string currentNamespace;

    bool hasMain = false;           //  Выставляется compileDecl, когда встречаем функцию main

    std::string newLabel(const std::string& hint = "L");    //  Генерирует уникальную метку
    std::string internString(const std::string& str);       //  Добавляет строку в пул, возвращает метку

    int sizeOfType(const std::shared_ptr<Type>& type) const;   //  Размер типа в байтах с учётом выравнивания
    static bool isDynArrayTypeName(TypeName* name);                    //  Верхнеуровневый T[]-тип
    static bool isFloatTypeName(TypeName* name);                       //  float/float32/float64 без суффиксов
    
    std::shared_ptr<Type> paramType(FuncDecl* func, size_t index) const;
    std::shared_ptr<Type> returnType(FuncDecl* func) const;
    std::shared_ptr<Type> exprType(Expr* e) const;
    std::shared_ptr<Type> varInitType(VarInit* v, VarDecl* decl) const;
    std::shared_ptr<Type> paramType(const Param& p) const;
    std::shared_ptr<Type> fieldType(FieldAccess* f) const;
    std::shared_ptr<Type> callReturnType(FuncCall* call) const;
    std::shared_ptr<FuncInfo> callFuncInfo(FuncCall* call) const;
    std::string symbolLabel(const std::shared_ptr<Symbol>& sym, const std::string& fallbackName) const;
    std::string mangleQualifiedName(const std::string& name) const;
    std::string mangleNamespaceAccess(const NamespaceAccess* access) const;

    std::vector<GlobalVarInit> globalVars;                                         //  Глобальные переменные в порядке объявления — для детерминированной инициализации
    void compileGlobalInit(VarDecl* decl, VarInit* var);                           //  Инициализация одной глобалки в прологе main
    bool emitDynArrayAddr(Expr* e, const char* reg);                              //  Адрес DynArray-источника (локалка / self-поле / ClassName.field / глобалка) в reg

    int allocLocal(const std::shared_ptr<Symbol>& sym, const std::string& nameHint, const std::shared_ptr<Type>& type);

    const LocalVar* findLocal(const std::shared_ptr<Symbol>& sym) const;

    std::string globalLabel(const std::shared_ptr<Symbol>& sym, const std::string& nameHint);
    bool isGlobal(const std::shared_ptr<Symbol>& sym) const;

    void collectLayout(const std::string& name, const std::vector<StructField>& fields); //  Запоминает layout полей struct/class (поле = qword)
    void collectDecls(Stmt* decl);                                                       //  Обходит AST и регистрирует layout'ы struct/class
    void compileDecl(Stmt* decl, const std::string& classPrefix);                        //  Компилирует функции/методы; classPrefix добавляется к имени символа

    std::vector<std::string> codegenErrors;

    bool hasRuntimeSizedArray(const std::shared_ptr<Type>& type) const;
    void validateCodegenType(const std::shared_ptr<Type>& type, int line, int column, const std::string& where);
    void validateDeclTypes(Stmt* decl);
    void validateStmtTypes(Stmt* stmt);

    void compileFunction(FuncDecl* func, bool isMethod = false);   //  Компиляция функции/метода; isMethod=true резервирует self в rdi
    void compileMethod(FuncDecl* f, const std::string& labelName);
    int countLocalsSize(Stmt* size);    //  Первый проход: суммарный размер локалок в теле (8-байтное выравнивание)
    void compileStmt(Stmt* stmt);   //  Компиляция одного стейтмента
    void compileExpr(Expr* expr);   //  Компиляция выражения — результат в rax (для int/ptr)
    void compileCompoundValue(Assign* assign, Expr* currentValue);  //  target op= value, результат в rax
    void compileClassConstruction(const std::string& className, const std::vector<Expr*>& args, const std::shared_ptr<FuncInfo>& ctorInfo = nullptr);

    std::expected<void, std::string> finalize(const std::string& outPath);  //  Финальная сборка: склеивает секции, пишет .asm, вызывает nasm + ld

public:
    //  Точка входа — принимает AST и путь выходного исполняемого файла
    std::expected<void, std::string> generate(Program* program, const std::string& outPath);
};
