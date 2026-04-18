#include "Tokens.hpp"
#include "Ast.hpp"
#include <vector>
#include <expected>

struct Parser {
    const std::vector<Token>& source;
    size_t i = 0;

    Parser(const std::vector<Token>& source) : source(source) {}

    int curLine() const { return i < source.size() ? source[i].line : 0; }

    //  Разбирает имя типа: int, int[], int[3], int[][], int[3][4], Point и т.д.
    //  Сначала читает базовый тип, потом цепочку суффиксов [] или [N]
    //  Возвращает строковое представление: "int", "int[]", "int[3]", "int[][3]"
    std::expected<std::string, std::string> parseTypeName() {
        //  Базовый тип: int, string, Point и т.д.
        if (i >= source.size() || (source[i].type != TokenType::TypeName && source[i].type != TokenType::Iden)) {
            return std::unexpected("Ошибка парсера: ожидалось имя типа");
        }
        std::string result = source[i++].lexeme;

        //  Суффиксы [] или [N] — могут повторяться (int[][], int[3][4])
        while (i < source.size() && source[i].type == TokenType::LeftBracket) {
            i++;  //  съели '['

            if (i < source.size() && source[i].type == TokenType::RightBracket) {
                //  T[] — динамический массив
                i++;  //  съели ']'
                result += "[]";
            }
            else if (i < source.size() && source[i].type == TokenType::Number) {
                //  T[N] — фиксированный массив
                std::string size = source[i++].lexeme;

                if (i >= source.size() || source[i].type != TokenType::RightBracket) {
                    return std::unexpected("Ошибка парсера: ожидалась ']' после размера массива");
                }
                i++;  //  съели ']'
                result += "[" + size + "]";
            }
            else {
                return std::unexpected("Ошибка парсера: ожидался ']' или размер в типе массива");
            }
        }

        return result;
    }

    std::expected<Stmt*, std::string> parseStatement() {
        if (i < source.size() && source[i].type == TokenType::If){
            return parseIf();
        }

        if (i < source.size() && source[i].type == TokenType::While){
            return parseWhile();
        }

        if (i < source.size() && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Const || source[i].type == TokenType::Auto)){
            return parseVarDecl();
        }

        // пользовательский тип: Iden Iden — это объявление переменной (Type name ...)
        if (i + 1 < source.size() && source[i].type == TokenType::Iden && source[i + 1].type == TokenType::Iden){
            return parseVarDecl();
        }

        // нулевая инструкция ";"
        if (i < source.size() && source[i].type == TokenType::Separator){
            auto* node = new ExprStmt();
            node->line = curLine();
            i++;
            return node;
        }

        if (i < source.size() && source[i].type == TokenType::Return){
            return parseReturn();
        }

        if (i < source.size() && source[i].type == TokenType::Break){
            return parseBreak();
        }

        if (i < source.size() && source[i].type == TokenType::Continue){
            return parseContinue();
        }

        // delete expr;
        if (i < source.size() && source[i].type == TokenType::Delete) {
            int ln = curLine();
            i++; // съели 'delete'
            auto expr = parseEquasion();
            if (!expr) return std::unexpected(expr.error());
            if (i >= source.size() || source[i].type != TokenType::Separator)
                return std::unexpected("Ошибка парсера: ожидался ';' после delete");
            i++; // съели ';'
            auto* node = new DeleteStmt();
            node->line = ln;
            node->value = *expr;
            return node;
        }

        if (i < source.size() && (source[i].type == TokenType::LeftBrace)){
            i++;
            return parseBlock();
        }

        auto expr = parseEquasion();

        if (!expr) return std::unexpected(expr.error());

        // присваивание: lvalue = expr ;
        if (i < source.size() && source[i].type == TokenType::Equal){
            i++; // съели '='

            auto rhs = parseEquasion();
            if (!rhs) return std::unexpected(rhs.error());

            if (i >= source.size() || source[i].type != TokenType::Separator){
                return std::unexpected("Ошибка парсера: ожидался ';' после присваивания");
            }
            i++;

            auto *node = new Assign;
            node->line = (*expr)->line;
            node->target = *expr;
            node->value = *rhs;
            return node;
        }

        // выражение как инструкция: expr ;
        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected("Ошибка парсера: ожидался ';' после выражения");
        }
        i++;

        auto *node = new ExprStmt;
        node->line = (*expr)->line;
        node->expr = *expr;
        return node;
    }

    std::expected<Stmt*, std::string> parseIf(){
        int ln = curLine();
        i++; // съели 'if'

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            return std::unexpected("Ожидалась '(' после if");
        }
        i++;

        auto cond = parseEquasion();
        if (!cond){
            return std::unexpected(cond.error());
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            return std::unexpected("Ожидалась ')'");
        }
        i++;
        
        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            return std::unexpected("Ожидалась '{' после условия if");
        }
        i++; // съели '{'

        auto thenBlock = parseBlock();
        if (!thenBlock){
            return std::unexpected(thenBlock.error());
        }

        Stmt* elseStmt = nullptr;

        if (i < source.size() && source[i].type == TokenType::Else){
            i++;
            if (i < source.size() && source[i].type == TokenType::If){
                auto elseIf = parseIf();
                if (!elseIf){
                    return std::unexpected(elseIf.error());
                }
                elseStmt = *elseIf;
            } else {
                if (i >= source.size() || source[i].type != TokenType::LeftBrace){
                    return std::unexpected("Ожидалась '{' после else");
                }
                i++; // съели '{'
                auto elseBlock = parseBlock();
                if (!elseBlock){
                    return std::unexpected(elseBlock.error());
                }
                elseStmt = *elseBlock;
            }
        }

        auto* node = new If();
        node->line = ln;
        node->condition = *cond;
        node->thenBranch = *thenBlock;
        node->elseBranch = elseStmt;

        return node;
    }

    std::expected<Stmt*, std::string> parseWhile(){
        int ln = curLine();
        i++; // съели 'while'

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            return std::unexpected("Ожидалась '(' после while");
        }
        i++;

        auto cond = parseEquasion();
        if (!cond){
            return std::unexpected(cond.error());
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            return std::unexpected("Ожидалась ')'");
        }
        i++;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            return std::unexpected("Ожидалась '{' после условия while");
        }
        i++; // съели '{'

        auto body = parseBlock();
        if (!body){
            return std::unexpected(body.error());
        }

        auto* node = new While();
        node->line = ln;
        node->condition = *cond;
        node->body = *body;

        return node;
    }

    std::expected<Stmt*, std::string> parseReturn(){
        int ln = curLine();
        i++; // съели 'return'

        auto *node = new Return();
        node->line = ln;
        node->value = nullptr;

        if (i < source.size() && source[i].type != TokenType::Separator){
            auto expr = parseEquasion();
            if (!expr){
                delete node;
                return std::unexpected(expr.error());
            }
            node->value = *expr;
        }

        if (i >= source.size() || source[i].type != TokenType::Separator){
            delete node;
            return std::unexpected("Ошибка парсера: ожидался ';' после return");
        }
        i++;

        return node;
    }

    std::expected<Stmt*, std::string> parseBreak(){
        int ln = curLine();
        i++; // съели 'break'

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected("Ошибка парсера: ожидался ';' после break");
        }
        i++;

        auto* node = new Break();
        node->line = ln;
        return node;
    }

    std::expected<Stmt*, std::string> parseContinue(){
        int ln = curLine();
        i++; // съели 'continue'

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected("Ошибка парсера: ожидался ';' после continue");
        }
        i++;

        auto* node = new Continue();
        node->line = ln;
        return node;
    }

    std::expected<Stmt*, std::string> parseFuncDecl(){
        auto node = new FuncDecl;
        node->line = curLine();

        {
            auto retType = parseTypeName();
            if (!retType) {
                delete node;
                return std::unexpected(retType.error());
            }
            node->returnType = *retType;
        }

        if (i >= source.size() || source[i].type != TokenType::Iden){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалось имя функции");
        }
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '(' после имени функции");
        }
        i++; // съели '('

        // парсим параметры: type name, type name, ...
        if (i < source.size() && source[i].type != TokenType::RightParen){
            while (true){
                auto paramType = parseTypeName();
                if (!paramType) {
                    delete node;
                    return std::unexpected(paramType.error());
                }
                std::string typeName = *paramType;

                if (i >= source.size() || source[i].type != TokenType::Iden){
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалось имя параметра");
                }
                std::string name = source[i++].lexeme;

                node->params.push_back({typeName, name});

                if (i < source.size() && source[i].type == TokenType::Comma){
                    i++; // съели ','
                } else {
                    break;
                }
            }
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась ')' после параметров");
        }
        i++; // съели ')'

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected("Ошибка парсера: ожидался '{' в теле функции");
        }
        i++; // съели '{'

        auto body = parseBlock();
        if (!body){
            delete node;
            return std::unexpected(body.error());
        }

        node->body = *body;
        return node;
    }

    std::expected<Stmt*, std::string> parseStructDecl(){
        int ln = curLine();
        i++; // съели 'struct'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected("Ошибка парсера: ожидалось имя структуры");
        }

        auto *node = new StructDecl();
        node->line = ln;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '{' в объявлении структуры");
        }
        i++; // съели '{'

        while (i < source.size() && source[i].type != TokenType::RightBrace){
            auto fieldType = parseTypeName();
            if (!fieldType) {
                delete node;
                return std::unexpected(fieldType.error());
            }
            std::string typeName = *fieldType;

            if (i >= source.size() || source[i].type != TokenType::Iden){
                delete node;
                return std::unexpected("Ошибка парсера: ожидалось имя поля структуры");
            }
            std::string fieldName = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::Separator){
                delete node;
                return std::unexpected("Ошибка парсера: ожидался ';' после поля структуры");
            }
            i++; // съели ';'

            node->fields.push_back({typeName, fieldName});
        }

        if (i >= source.size()){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '}' в объявлении структуры");
        }
        i++; // съели '}'

        return node;
    }

    std::expected<Stmt*, std::string> parseClassDecl(){
        int ln = curLine();
        i++; // съели 'class'

        if (i >= source.size() || source[i].type != TokenType::Iden)
            return std::unexpected("Ошибка парсера: ожидалось имя класса");

        auto* node = new ClassDecl();
        node->line = ln;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '{' в объявлении класса");
        }
        i++; // съели '{'

        while (i < source.size() && source[i].type != TokenType::RightBrace) {
            //  Деструктор: ~ClassName()
            if (i + 1 < source.size() && source[i].type == TokenType::Tilde
                && source[i + 1].type == TokenType::Iden && source[i + 1].lexeme == node->name) {
                i++; // съели '~'
                i++; // съели имя

                if (i >= source.size() || source[i].type != TokenType::LeftParen) {
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась '(' после имени деструктора");
                }
                i++; // съели '('
                if (i >= source.size() || source[i].type != TokenType::RightParen) {
                    delete node;
                    return std::unexpected("Ошибка парсера: деструктор не принимает параметров");
                }
                i++; // съели ')'

                if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась '{' в теле деструктора");
                }
                i++; // съели '{'

                auto body = parseBlock();
                if (!body) { 
                    delete node; 
                    return std::unexpected(body.error()); 
                }

                auto* dtor = new FuncDecl();
                dtor->line = ln;
                dtor->returnType = "void";
                dtor->name = "~" + node->name;
                dtor->body = dynamic_cast<Block*>(*body);
                node->destructor = dtor;
                continue;
            }

            //  Конструктор: ClassName(params)
            if (i + 1 < source.size() && source[i].type == TokenType::Iden
                && source[i].lexeme == node->name && source[i + 1].type == TokenType::LeftParen) {
                i++; // съели имя
                i++; // съели '('

                //  Парсим параметры
                std::vector<Param> params;
                if (i < source.size() && source[i].type != TokenType::RightParen) {
                    while (true) {
                        auto paramType = parseTypeName();
                        if (!paramType) { delete node; return std::unexpected(paramType.error()); }

                        if (i >= source.size() || source[i].type != TokenType::Iden) {
                            delete node;
                            return std::unexpected("Ошибка парсера: ожидалось имя параметра конструктора");
                        }
                        params.push_back({*paramType, source[i++].lexeme});

                        if (i < source.size() && source[i].type == TokenType::Comma)
                            i++;
                        else
                            break;
                    }
                }
                if (i >= source.size() || source[i].type != TokenType::RightParen) {
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась ')' после параметров конструктора");
                }
                i++; // съели ')'

                if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась '{' в теле конструктора");
                }
                i++; // съели '{'

                auto body = parseBlock();
                if (!body) { delete node; return std::unexpected(body.error()); }

                auto* ctor = new FuncDecl();
                ctor->line = ln;
                ctor->returnType = "void";
                ctor->name = node->name;
                ctor->params = params;
                ctor->body = dynamic_cast<Block*>(*body);
                node->constructor = ctor;
                continue;
            }

            //  Метод: type name(params) { ... }
            if (i + 2 < source.size()
                && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Iden)
                && source[i + 1].type == TokenType::Iden
                && source[i + 2].type == TokenType::LeftParen) {
                auto method = parseFuncDecl();
                if (!method) { delete node; return std::unexpected(method.error()); }
                node->methods.push_back(dynamic_cast<FuncDecl*>(*method));
                continue;
            }

            //  Поле: type name;
            auto fieldType = parseTypeName();
            if (!fieldType) { delete node; return std::unexpected(fieldType.error()); }

            if (i >= source.size() || source[i].type != TokenType::Iden) {
                delete node;
                return std::unexpected("Ошибка парсера: ожидалось имя поля класса");
            }
            std::string fieldName = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::Separator) {
                delete node;
                return std::unexpected("Ошибка парсера: ожидался ';' после поля класса");
            }
            i++; // съели ';'

            node->fields.push_back({*fieldType, fieldName});
        }

        if (i >= source.size()) {
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '}' в объявлении класса");
        }
        i++; // съели '}'

        return node;
    }

    std::expected<Stmt*, std::string> parseTypeAlias(){
        int ln = curLine();
        i++; // съели 'type'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected("Ошибка парсера: ожидалось имя алиаса");
        }
        std::string alias = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::Equal){
            return std::unexpected("Ошибка парсера: ожидался '=' в type alias");
        }
        i++; // съели '='

        if (i >= source.size() || source[i].type != TokenType::TypeName){
            return std::unexpected("Ошибка парсера: ожидался тип в type alias");
        }
        std::string original = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected("Ошибка парсера: ожидался ';' после type alias");
        }
        i++; // съели ';'

        auto *node = new TypeAlias();
        node->line = ln;
        node->alias = alias;
        node->original = original;
        return node;
    }

    std::expected<Stmt*, std::string> parseNamespaceDecl(){
        int ln = curLine();
        i++; // съели 'namespace'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected("Ошибка парсера: ожидалось имя namespace");
        }

        auto *node = new NamespaceDecl();
        node->line = ln;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '{' в namespace");
        }
        i++; // съели '{'

        while (i < source.size() && source[i].type != TokenType::RightBrace){
            auto decl = parseTopDecl();
            if (!decl){
                delete node;
                return std::unexpected(decl.error());
            }
            node->decls.push_back(*decl);
        }

        if (i >= source.size()){
            delete node;
            return std::unexpected("Ошибка парсера: ожидалась '}' в namespace");
        }
        i++; // съели '}'

        return node;
    }

    // Различает VarDecl и FuncDecl: type name "(" → функция, иначе → переменная
    std::expected<Stmt*, std::string> parseVarOrFuncDecl(){
        // const → всегда переменная
        if (i < source.size() && source[i].type == TokenType::Const){
            return parseVarDecl();
        }

        // смотрим вперёд: type name "(" → функция
        // Но если после type идёт '[' — это массивный тип (int[] arr), всегда переменная
        if (i + 2 < source.size() &&
            (source[i].type == TokenType::TypeName || source[i].type == TokenType::Iden) &&
            source[i + 1].type == TokenType::Iden &&
            source[i + 2].type == TokenType::LeftParen){
            return parseFuncDecl();
        }

        return parseVarDecl();
    }

    std::expected<Stmt*, std::string> parseImportDecl(){
        int ln = curLine();
        i++; // съели 'import'

        if (i >= source.size()){
            return std::unexpected("Ошибка парсера: ожидался путь после 'import'");
        }

        auto *node = new ImportDecl();
        node->line = ln;

        if (source[i].type == TokenType::Less){
            //  C-импорт: import <header.h>
            node->isC = true;
            i++; // съели '<'
            std::string header;
            while (i < source.size() && source[i].type != TokenType::Greater){
                header += source[i].lexeme;
                i++;
            }
            if (i >= source.size()){
                delete node;
                return std::unexpected("Ошибка парсера: ожидался '>' в C-импорте");
            }
            i++; // съели '>'
            if (header.empty()){
                delete node;
                return std::unexpected("Ошибка парсера: пустой путь в C-импорте");
            }
            node->path = header;
        }
        else if (source[i].type == TokenType::StringLit){
            node->path = source[i++].lexeme;
        }
        else {
            delete node;
            return std::unexpected("Ошибка парсера: ожидался путь к модулю после 'import'");
        }

        if (i >= source.size() || source[i].type != TokenType::Separator){
            delete node;
            return std::unexpected("Ошибка парсера: ожидался ';' после import");
        }
        i++;

        return node;
    }

    std::expected<Stmt*, std::string> parseTopDecl(){
        // export обёртка
        if (i < source.size() && source[i].type == TokenType::Import){
            return parseImportDecl();
        }
        if (i < source.size() && source[i].type == TokenType::Export){
            i++; // съели 'export'

            auto inner = parseTopDecl();
            if (!inner) return inner;

            auto *node = new ExportDecl();
            node->line = curLine();
            node->decl = *inner;
            return node;
        }
        if (i < source.size() && source[i].type == TokenType::Struct){
            return parseStructDecl();
        }
        if (i < source.size() && source[i].type == TokenType::Class){
            return parseClassDecl();
        }
        if (i < source.size() && source[i].type == TokenType::Type){
            return parseTypeAlias();
        }
        if (i < source.size() && source[i].type == TokenType::Namespace){
            return parseNamespaceDecl();
        }
        // auto → всегда переменная
        if (i < source.size() && source[i].type == TokenType::Auto){
            return parseVarDecl();
        }
        // var_decl или func_decl — оба начинаются с [const] type iden
        if (i < source.size() && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Const)){
            return parseVarOrFuncDecl();
        }
        // пользовательский тип: Iden Iden на верхнем уровне
        if (i + 1 < source.size() && source[i].type == TokenType::Iden && source[i + 1].type == TokenType::Iden){
            return parseVarOrFuncDecl();
        }
        return std::unexpected("Ошибка парсера: ожидалось объявление верхнего уровня, получен '" + source[i].lexeme + "'");
    }

    std::expected<Stmt*, std::string> parseVarDecl(){
        auto node = new VarDecl();
        node->line = curLine();

        if (i < source.size() && source[i].type == TokenType::Const){
            i++;
            node->isConst = true;
        }

        if (i < source.size() && source[i].type == TokenType::Auto){
            i++;
            node->isAuto = true;
        }
        else {
            //  Разбираем тип: int, [int], [int; 3], Point и т.д.
            auto typeName = parseTypeName();
            if (!typeName) {
                delete node;
                return std::unexpected(typeName.error());
            }
            node->typeName = *typeName;
        }

        if (i < source.size() && (source[i].type == TokenType::Iden)){
            node->name = source[i++].lexeme;
        }
        else {
            delete node;
            return std::unexpected("Ошибка парсера: ожидался идентификатор в объявлении переменной");
        }

        if (i < source.size() && (source[i].type == TokenType::Equal)){
            i++;
            auto init = parseEquasion();
           
            if (!init) { 
                delete node; 
                return std::unexpected(init.error()); 
            }
           
            node->init = *init;
        }

        if (i >= source.size() || source[i].type != TokenType::Separator){
            delete node;
            return std::unexpected("Ошибка парсера: ожидался ';' после объявления переменной");
        }
        i++;

        return node;
    }

    std::expected<Block*, std::string> parseBlock(){
        auto block = new Block;
        block->line = curLine();

        while (i < source.size() && (source[i].type != TokenType::RightBrace)){
            auto stmt = parseStatement();

            if(!stmt){
                delete block;
                return std::unexpected(stmt.error());
            }

            block->statements.push_back(stmt.value());
        }
        
        if (i >= source.size()){
            delete block;
            return std::unexpected("Expected '}'");
        }
        i++;

        return block;
    }

    std::expected<Expr*, std::string> parseEquasion() {
        return parseLogicOr();
    }

    std::expected<Expr*, std::string> parseLogicOr(){
        auto left = parseLogicAnd();
        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        while (i < source.size() && source[i].type == TokenType::Or){
            i++;
            auto right = parseLogicAnd();
            if (!right){
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->op = Operand::Or;
            node->left = result;
            node->right = *right;
            result = node;
        }

        return result;
    }

    std::expected<Expr*, std::string> parseLogicAnd(){
        auto left = parseEquality();
        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        while (i < source.size() && source[i].type == TokenType::And){
            i++;
            auto right = parseEquality();
            if (!right){
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->op = Operand::And;
            node->left = result;
            node->right = *right;
            result = node;
        }

        return result;
    }

    std::expected<Expr*, std::string> parseEquality(){
        auto left = parseComparison();
        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        while (i < source.size() && (source[i].type == TokenType::EqualEqual || source[i].type == TokenType::NotEqual)){

            Operand op = (source[i].type == TokenType::EqualEqual) ? Operand::EqualEqual : Operand::NotEqual;
            i++;

            auto right = parseComparison();
            if (!right){
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->op = op;
            node->left = result;
            node->right = *right;
            result = node;
        }

        return result;
    }

    std::expected<Expr*, std::string> parseComparison(){
        auto left = parseAddSub();
        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        while (i < source.size() &&
            (source[i].type == TokenType::Less ||
            source[i].type == TokenType::LessEqual ||
            source[i].type == TokenType::Greater ||
            source[i].type == TokenType::GreaterEqual)){

            Operand op;

            switch (source[i].type){
                case TokenType::Less: op = Operand::Less; break;
                case TokenType::LessEqual: op = Operand::LessEqual; break;
                case TokenType::Greater: op = Operand::Greater; break;
                case TokenType::GreaterEqual: op = Operand::GreaterEqual; break;
                default: break;
            }

            i++;

            auto right = parseAddSub();
            if (!right){
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->op = op;
            node->left = result;
            node->right = *right;
            result = node;
        }

        return result;
    }

    std::expected<Expr*, std::string> parseAddSub() {
        auto left = parseMulDiv();
        
        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        while (i < source.size() && (source[i].type == TokenType::Plus || source[i].type == TokenType::Minus)) {
            Operand op = (source[i].type == TokenType::Plus) ? Operand::Add : Operand::Sub;
            i++;
            auto right = parseMulDiv();
            
            if (!right) {
                return std::unexpected(right.error());
            }
            
            auto *node = new Binary();
            node->line = result->line;
            node->op = op;
            node->left = result;
            node->right = *right;
            result = node;
        }
        return result;
    }

    std::expected<Expr*, std::string> parseMulDiv() {
        auto left = parseUnary();

        if (!left){
            return std::unexpected(left.error());
        }
        
        Expr* result = *left;

        while (i < source.size() && (source[i].type == TokenType::Multiply || source[i].type == TokenType::Divide || source[i].type == TokenType::Modulo)) {
            Operand op;
            if (source[i].type == TokenType::Multiply) op = Operand::Mul;
            else if (source[i].type == TokenType::Divide) op = Operand::Div;
            else op = Operand::Mod;
            i++;
            auto right = parseUnary();
            
            if (!right) {
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->op = op;
            node->left = result;
            node->right = *right;
            result = node;
        }
        return result;
    }

    std::expected<Expr*, std::string> parseUnary(){
        if(i < source.size()){
            if (source[i].type == TokenType::Minus){
                int ln = curLine();
                i++;
                auto right = parseUnary();

                if (!right){
                    return std::unexpected(right.error());
                }

                auto *node = new Unary();
                node->line = ln;
                node->op = Operand::UnaryMinus;
                node->operand = *right;
                return node;
            }
            if (source[i].type == TokenType::Plus){
                i++;
                return parseUnary();
            }
            if (source[i].type == TokenType::Not){
                int ln = curLine();
                i++;
                auto right = parseUnary();

                if (!right){
                    return std::unexpected(right.error());
                }

                auto *node = new Unary();
                node->line = ln;
                node->op = Operand::Not;
                node->operand = *right;
                return node;
            }
        }
        return parsePostfix();
    }

    std::expected<Expr*, std::string> parsePostfix(){
        auto expr = parsePrimary();
        if (!expr){
            return std::unexpected(expr.error());
        }

        Expr* result = *expr;

        while (i < source.size()){
            if (source[i].type == TokenType::Dot){
                i++; // съели '.'

                if (i >= source.size() || source[i].type != TokenType::Iden){
                    return std::unexpected("Ошибка парсера: ожидалось имя поля после '.'");
                }

                auto *node = new FieldAccess();
                node->line = result->line;
                node->object = result;
                node->field = source[i++].lexeme;
                result = node;

            } 
            else if (source[i].type == TokenType::LeftBracket){
                i++; // съели '['

                auto index = parseEquasion();
                if (!index){
                    return std::unexpected(index.error());
                }

                if (i >= source.size() || source[i].type != TokenType::RightBracket){
                    return std::unexpected("Ошибка парсера: ожидалась ']'");
                }
                i++; // съели ']'

                auto *node = new ArrayAccess();
                node->line = result->line;
                node->object = result;
                node->index = *index;
                result = node;

            } 
            else if (source[i].type == TokenType::LeftParen){
                i++; // съели '('

                auto *node = new FuncCall();
                node->line = result->line;
                node->callee = result;

                // парсим аргументы: expr, expr, ...
                if (i < source.size() && source[i].type != TokenType::RightParen){
                    while (true){
                        auto arg = parseEquasion();
                        if (!arg){
                            delete node;
                            return std::unexpected(arg.error());
                        }
                        node->args.push_back(*arg);

                        if (i < source.size() && source[i].type == TokenType::Comma){
                            i++; // съели ','
                        } else {
                            break;
                        }
                    }
                }

                if (i >= source.size() || source[i].type != TokenType::RightParen){
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась ')' после аргументов вызова");
                }
                i++; // съели ')'

                result = node;
                break; // вызов функции — конец цепочки

            }
            else if (source[i].type == TokenType::PlusPlus){
                i++; // съели '++'

                auto *node = new Unary();
                node->line = result->line;
                node->op = Operand::Increment;
                node->operand = result;
                result = node;
                break; // постфиксный инкремент — конец цепочки
            }
            else if (source[i].type == TokenType::MinusMinus){
                i++; // съели '--'

                auto *node = new Unary();
                node->line = result->line;
                node->op = Operand::Decrement;
                node->operand = result;
                result = node;
                break; // постфиксный декремент — конец цепочки
            }  
            else {
                break;
            }
        }

        return result;
    }

    std::expected<Expr*, std::string> parsePrimary() {
        if (i >= source.size()){
            return std::unexpected("Ошибка парсера: неожиданный конец файла");
        }

        // числовой литерал
        if (source[i].type == TokenType::Number){
            auto *node = new Number();
            node->line = curLine();
            node->isFloat = (source[i].subType == SubType::Float);
            node->value = std::stod(source[i++].lexeme);
            return node;
        }

        // строковый литерал
        if (source[i].type == TokenType::StringLit){
            auto *node = new String();
            node->line = curLine();
            node->value = source[i++].lexeme;
            return node;
        }

        // булев литерал
        if (source[i].type == TokenType::BoolLit){
            auto *node = new Bool();
            node->line = curLine();
            node->value = (source[i++].lexeme == "true");
            return node;
        }

        // cast<type>(expr)
        if (source[i].type == TokenType::Cast){
            int ln = curLine();
            i++; // съели 'cast'

            if (i >= source.size() || source[i].type != TokenType::Less){
                return std::unexpected("Ошибка парсера: ожидалась '<' после cast");
            }
            i++; // съели '<'

            if (i >= source.size() || source[i].type != TokenType::TypeName){
                return std::unexpected("Ошибка парсера: ожидался тип в cast<>");
            }
            std::string targetType = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::Greater){
                return std::unexpected("Ошибка парсера: ожидалась '>' в cast<>");
            }
            i++; // съели '>'

            if (i >= source.size() || source[i].type != TokenType::LeftParen){
                return std::unexpected("Ошибка парсера: ожидалась '(' после cast<type>");
            }
            i++; // съели '('

            auto expr = parseEquasion();
            if (!expr){
                return std::unexpected(expr.error());
            }

            if (i >= source.size() || source[i].type != TokenType::RightParen){
                return std::unexpected("Ошибка парсера: ожидалась ')' в cast");
            }
            i++; // съели ')'

            auto *node = new CastExpr();
            node->line = ln;
            node->targetType = targetType;
            node->value = *expr;
            return node;
        }

        // new ClassName(args...)
        if (source[i].type == TokenType::New) {
            int ln = curLine();
            i++; // съели 'new'

            if (i >= source.size() || source[i].type != TokenType::Iden)
                return std::unexpected("Ошибка парсера: ожидалось имя класса после 'new'");
            auto* node = new NewExpr();
            node->line = ln;
            node->className = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::LeftParen) {
                delete node;
                return std::unexpected("Ошибка парсера: ожидалась '(' после имени класса в new");
            }
            i++; // съели '('

            if (i < source.size() && source[i].type != TokenType::RightParen) {
                while (true) {
                    auto arg = parseEquasion();
                    if (!arg) { delete node; return std::unexpected(arg.error()); }
                    node->args.push_back(*arg);
                    if (i < source.size() && source[i].type == TokenType::Comma)
                        i++;
                    else
                        break;
                }
            }

            if (i >= source.size() || source[i].type != TokenType::RightParen) {
                delete node;
                return std::unexpected("Ошибка парсера: ожидалась ')' в new");
            }
            i++; // съели ')'
            return node;
        }

        // идентификатор, namespace access, struct literal
        if (source[i].type == TokenType::Iden){
            int ln = curLine();
            std::string name = source[i++].lexeme;

            // iden "::" iden — доступ к namespace
            if (i < source.size() && source[i].type == TokenType::ColonColon){
                i++; // съели '::'

                if (i >= source.size() || source[i].type != TokenType::Iden){
                    return std::unexpected("Ошибка парсера: ожидался идентификатор после '::'");
                }

                auto *node = new NamespaceAccess();
                node->line = ln;
                node->nameSpace = name;
                node->member = source[i++].lexeme;
                return node;
            }

            // iden "{" field_init_list "}" — литерал структуры
            if (i < source.size() && source[i].type == TokenType::LeftBrace){
                i++; // съели '{'

                auto *node = new StructLiteral();
                node->line = ln;
                node->name = name;

                if (i < source.size() && source[i].type != TokenType::RightBrace){
                    while (true){
                        if (i >= source.size() || source[i].type != TokenType::Iden){
                            delete node;
                            return std::unexpected("Ошибка парсера: ожидалось имя поля в литерале структуры");
                        }
                        std::string fieldName = source[i++].lexeme;

                        if (i >= source.size() || source[i].type != TokenType::Colon){
                            delete node;
                            return std::unexpected("Ошибка парсера: ожидался ':' после имени поля");
                        }
                        i++; // съели ':'

                        auto val = parseEquasion();
                        if (!val){
                            delete node;
                            return std::unexpected(val.error());
                        }

                        node->fields.push_back({fieldName, *val});

                        if (i < source.size() && source[i].type == TokenType::Comma){
                            i++; // съели ','
                        } else {
                            break;
                        }
                    }
                }

                if (i >= source.size() || source[i].type != TokenType::RightBrace){
                    delete node;
                    return std::unexpected("Ошибка парсера: ожидалась '}' в литерале структуры");
                }
                i++; // съели '}'

                return node;
            }

            // просто идентификатор
            auto *node = new Identifier();
            node->line = ln;
            node->name = name;
            return node;
        }

        // "[" expr_list "]" — литерал массива
        if (source[i].type == TokenType::LeftBracket){
            int ln = curLine();
            i++; // съели '['

            auto *node = new ArrayLiteral();
            node->line = ln;

            if (i < source.size() && source[i].type != TokenType::RightBracket){
                while (true){
                    auto elem = parseEquasion();
                    if (!elem){
                        delete node;
                        return std::unexpected(elem.error());
                    }
                    node->elements.push_back(*elem);

                    if (i < source.size() && source[i].type == TokenType::Comma){
                        i++; // съели ','
                    } else {
                        break;
                    }
                }
            }

            if (i >= source.size() || source[i].type != TokenType::RightBracket){
                delete node;
                return std::unexpected("Ошибка парсера: ожидалась ']'");
            }
            i++; // съели ']'

            return node;
        }

        // "(" expr ")" — группировка
        if (source[i].type == TokenType::LeftParen) {
            i++; // съели '('
            auto node = parseEquasion();

            if (!node) {
                return std::unexpected(node.error());
            }

            if (i >= source.size() || source[i].type != TokenType::RightParen){
                return std::unexpected("Ошибка парсера: ожидалась ')'");
            }
            i++; // съели ')'
            return *node;
        }

        return std::unexpected("Ошибка парсера: неожиданный токен '" + source[i].lexeme + "'");
    }
};

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source){
    Parser head(source);
    std::vector<Stmt*> declarations;

    while (head.i < source.size() && source[head.i].type != TokenType::End) {
        auto decl = head.parseTopDecl();

        if (!decl) {
            return std::unexpected(decl.error());
        }

        declarations.push_back(*decl);
    }

    return declarations;
}
