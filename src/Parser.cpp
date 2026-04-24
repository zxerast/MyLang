#include "Tokens.hpp"
#include "Ast.hpp"
#include <vector>
#include <expected>

struct Parser {
    const std::vector<Token>& source;
    const std::string filePath;
    size_t i = 0;

    Parser(const std::vector<Token>& source, const std::string& filePath) : source(source), filePath(filePath) {}

    //  Проверяем, начинается ли с текущей позиции последовательность:
    //  (Iden|TypeName) {"::" Iden} { "[" ... "]" } Iden
    //  Такая последовательность — объявление переменной/функции (тип + имя).
    bool looksLikeTypedDecl(size_t startIdx) const {
        size_t j = startIdx;
        if (j >= source.size() || (source[j].type != TokenType::Iden && source[j].type != TokenType::TypeName))
            return false;
        j++;
        while (j + 1 < source.size() && source[j].type == TokenType::ColonColon && source[j + 1].type == TokenType::Iden){
            j += 2;
        }
        while (j < source.size() && source[j].type == TokenType::LeftBracket){
            int depth = 1;
            j++;
            while (j < source.size() && depth > 0){
                if (source[j].type == TokenType::LeftBracket) depth++;
                else if (source[j].type == TokenType::RightBracket) depth--;
                j++;
            }
        }
        return j < source.size() && source[j].type == TokenType::Iden;
    }

    int curLine() const {
        if (i < source.size()) return source[i].line;
        return 0;
    }

    int curColumn() const {
        if (i < source.size()) return source[i].column;
        return 0;
    }

    //  Разбирает имя типа: int, int[], int[3], int[][], int[3][4], Point и т.д.
    //  Сначала читает базовый тип, потом цепочку суффиксов [] или [N]
    //  Возвращает строковое представление: "int", "int[]", "int[3]", "int[][3]"
    std::expected<std::string, std::string> parseTypeName() {
        //  Базовый тип: int, string, Point, Math::Vector2D и т.д.
        if (i >= source.size() || (source[i].type != TokenType::TypeName && source[i].type != TokenType::Iden)) {
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected type name");
        }
        std::string result = source[i++].lexeme;

        //  Namespace-qualified: A::B::C
        while (i + 1 < source.size() && source[i].type == TokenType::ColonColon
               && (source[i + 1].type == TokenType::Iden || source[i + 1].type == TokenType::TypeName)) {
            i++; // съели '::'
            result += "::" + source[i++].lexeme;
        }

        //  Суффиксы [] или [expr] — могут повторяться (int[][], int[3][4], int[size * 2])
        while (i < source.size() && source[i].type == TokenType::LeftBracket) {
            i++;  //  съели '['

            if (i < source.size() && source[i].type == TokenType::RightBracket) {
                //  T[] — динамический массив
                i++;  //  съели ']'
                result += "[]";
            }
            else {
                //  T[expr] — фиксированный массив с выражением-размером.
                //  Собираем токены до парной ']', считая вложенность '['.
                std::string sizeStr;
                int depth = 1;
                while (i < source.size() && depth > 0) {
                    if (source[i].type == TokenType::LeftBracket) { depth++; sizeStr += source[i++].lexeme; continue; }
                    if (source[i].type == TokenType::RightBracket) {
                        depth--;
                        if (depth == 0) break;
                        sizeStr += source[i++].lexeme;
                        continue;
                    }
                    if (!sizeStr.empty()) sizeStr += " ";
                    sizeStr += source[i++].lexeme;
                }
                if (i >= source.size() || source[i].type != TokenType::RightBracket) {
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ']' after array size");
                }
                i++;  //  съели ']'
                if (sizeStr.empty()){
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected array size expression");
                }
                result += "[" + sizeStr + "]";
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

        if (i < source.size() && source[i].type == TokenType::Type){
            return parseTypeAlias();
        }

        // пользовательский тип: Iden [::Iden]* [суффиксы массива]* Iden — объявление переменной
        if (i < source.size() && source[i].type == TokenType::Iden && looksLikeTypedDecl(i)){
            return parseVarDecl();
        }

        // нулевая инструкция ";"
        if (i < source.size() && source[i].type == TokenType::Separator){
            auto* node = new ExprStmt();
            node->line = curLine(); node->column = curColumn();
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
            int ln = curLine(); int col = curColumn();
            i++; // съели 'delete'
            auto expr = parseEquasion();
            if (!expr) return std::unexpected(expr.error());
            if (i >= source.size() || source[i].type != TokenType::Separator)
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after delete");
            i++; // съели ';'
            auto* node = new DeleteStmt();
            node->line = ln; node->column = col;
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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after assignment");
            }
            i++;

            auto *node = new Assign;
            node->line = (*expr)->line; node->column = (*expr)->column;
            node->target = *expr;
            node->value = *rhs;
            return node;
        }

        // составное присваивание: lvalue += expr ; и т.п. → lvalue = lvalue op expr
        if (i < source.size() && (source[i].type == TokenType::PlusEqual || source[i].type == TokenType::MinusEqual
            || source[i].type == TokenType::MulEqual || source[i].type == TokenType::DivEqual
            || source[i].type == TokenType::ModEqual)){
            Operand op;
            switch (source[i].type){
                case TokenType::PlusEqual:  op = Operand::Add; break;
                case TokenType::MinusEqual: op = Operand::Sub; break;
                case TokenType::MulEqual:   op = Operand::Mul; break;
                case TokenType::DivEqual:   op = Operand::Div; break;
                case TokenType::ModEqual:   op = Operand::Mod; break;
                default: op = Operand::Add; break;
            }
            i++;

            auto rhs = parseEquasion();
            if (!rhs) return std::unexpected(rhs.error());

            if (i >= source.size() || source[i].type != TokenType::Separator){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after compound assignment");
            }
            i++;

            auto *binary = new Binary();
            binary->line = (*expr)->line; binary->column = (*expr)->column;
            binary->op = op;
            binary->left = *expr;
            binary->right = *rhs;

            auto *node = new Assign();
            node->line = (*expr)->line; node->column = (*expr)->column;
            node->target = *expr;
            node->value = binary;
            return node;
        }

        // выражение как инструкция: expr ;
        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after expression");
        }
        i++;

        auto *node = new ExprStmt;
        node->line = (*expr)->line; node->column = (*expr)->column;
        node->expr = *expr;
        return node;
    }

    std::expected<Stmt*, std::string> parseIf(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'if'

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after 'if'");
        }
        i++;

        auto cond = parseEquasion();
        if (!cond){
            return std::unexpected(cond.error());
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' after if condition");
        }
        i++;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' after if condition");
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
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' after else");
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
        node->line = ln; node->column = col;
        node->condition = *cond;
        node->thenBranch = *thenBlock;
        node->elseBranch = elseStmt;

        return node;
    }

    std::expected<Stmt*, std::string> parseWhile(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'while'

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after 'while'");
        }
        i++;

        auto cond = parseEquasion();
        if (!cond){
            return std::unexpected(cond.error());
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' after while condition");
        }
        i++;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' after while condition");
        }
        i++; // съели '{'

        auto body = parseBlock();
        if (!body){
            return std::unexpected(body.error());
        }

        auto* node = new While();
        node->line = ln; node->column = col;
        node->condition = *cond;
        node->body = *body;

        return node;
    }

    std::expected<Stmt*, std::string> parseReturn(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'return'

        auto *node = new Return();
        node->line = ln; node->column = col;
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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after return");
        }
        i++;

        return node;
    }

    std::expected<Stmt*, std::string> parseBreak(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'break'

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after break");
        }
        i++;

        auto* node = new Break();
        node->line = ln; node->column = col;
        return node;
    }

    std::expected<Stmt*, std::string> parseContinue(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'continue'

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after continue");
        }
        i++;

        auto* node = new Continue();
        node->line = ln; node->column = col;
        return node;
    }

    std::expected<Stmt*, std::string> parseFuncDecl(){
        auto node = new FuncDecl;
        node->line = curLine(); node->column = curColumn();

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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected function name");
        }
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftParen){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after function name");
        }
        i++; // съели '('

        // парсим параметры: [const] type name [= expr], ...
        if (i < source.size() && source[i].type != TokenType::RightParen){
            while (true){
                bool isConst = false;
                if (i < source.size() && source[i].type == TokenType::Const){
                    isConst = true;
                    i++;
                }

                auto paramType = parseTypeName();
                if (!paramType) {
                    delete node;
                    return std::unexpected(paramType.error());
                }
                std::string typeName = *paramType;

                if (i >= source.size() || source[i].type != TokenType::Iden){
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected parameter name");
                }
                std::string name = source[i++].lexeme;

                Expr* defaultValue = nullptr;
                if (i < source.size() && source[i].type == TokenType::Equal){
                    i++; // съели '='
                    auto def = parseEquasion();
                    if (!def){ delete node; return std::unexpected(def.error()); }
                    defaultValue = *def;
                }

                node->params.push_back({typeName, name, defaultValue, isConst});

                if (i < source.size() && source[i].type == TokenType::Comma){
                    i++; // съели ','
                } else {
                    break;
                }
            }
        }

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' after parameters");
        }
        i++; // съели ')'

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in function body");
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
        int ln = curLine(); int col = curColumn();
        i++; // съели 'struct'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected struct name");
        }

        auto *node = new StructDecl();
        node->line = ln; node->column = col;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in struct declaration");
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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected struct field name");
            }
            std::string fieldName = source[i++].lexeme;

            Expr* defaultValue = nullptr;
            if (i < source.size() && source[i].type == TokenType::Equal) {
                i++; // съели '='
                auto initExpr = parseEquasion();
                if (!initExpr) {
                    delete node;
                    return std::unexpected(initExpr.error());
                }
                defaultValue = *initExpr;
            }

            if (i >= source.size() || source[i].type != TokenType::Separator){
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after struct field");
            }
            i++; // съели ';'

            node->fields.push_back({typeName, fieldName, defaultValue});
        }

        if (i >= source.size()){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in struct declaration");
        }
        i++; // съели '}'

        return node;
    }

    std::expected<Stmt*, std::string> parseClassDecl(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'class'

        if (i >= source.size() || source[i].type != TokenType::Iden)
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected class name");

        auto* node = new ClassDecl();
        node->line = ln; node->column = col;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in class declaration");
        }
        i++; // съели '{'

        while (i < source.size() && source[i].type != TokenType::RightBrace) {
            //  Пропускаем лишние ';' между членами класса
            if (source[i].type == TokenType::Separator) {
                i++;
                continue;
            }

            //  Деструктор: ~ClassName()
            if (i + 1 < source.size() && source[i].type == TokenType::Tilde && source[i + 1].type == TokenType::Iden && source[i + 1].lexeme == node->name) {
                i++; // съели '~'
                i++; // съели имя

                if (i >= source.size() || source[i].type != TokenType::LeftParen) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after destructor name");
                }
                i++; // съели '('
                if (i >= source.size() || source[i].type != TokenType::RightParen) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: destructor takes no parameters");
                }
                i++; // съели ')'

                if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in destructor body");
                }
                i++; // съели '{'

                auto body = parseBlock();
                if (!body) { 
                    delete node; 
                    return std::unexpected(body.error()); 
                }

                auto* dtor = new FuncDecl();
                dtor->line = ln; dtor->column = col;
                dtor->returnType = "void";
                dtor->name = "~" + node->name;
                dtor->body = dynamic_cast<Block*>(*body);
                node->destructor = dtor;
                continue;
            }

            //  Конструктор: ClassName(params)
            if (i + 1 < source.size() && source[i].type == TokenType::Iden && source[i].lexeme == node->name && source[i + 1].type == TokenType::LeftParen) {
                i++; // съели имя
                i++; // съели '('

                //  Парсим параметры
                std::vector<Param> params;
                if (i < source.size() && source[i].type != TokenType::RightParen) {
                    while (true) {
                        bool isConst = false;
                        if (i < source.size() && source[i].type == TokenType::Const){ isConst = true; i++; }

                        auto paramType = parseTypeName();
                        if (!paramType) {
                            delete node; return std::unexpected(paramType.error());
                        }

                        if (i >= source.size() || source[i].type != TokenType::Iden) {
                            delete node;
                            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected constructor parameter name");
                        }
                        std::string pname = source[i++].lexeme;

                        Expr* defaultValue = nullptr;
                        if (i < source.size() && source[i].type == TokenType::Equal){
                            i++;
                            auto def = parseEquasion();
                            if (!def){ delete node; return std::unexpected(def.error()); }
                            defaultValue = *def;
                        }
                        params.push_back({*paramType, pname, defaultValue, isConst});

                        if (i < source.size() && source[i].type == TokenType::Comma)
                            i++;
                        else
                            break;
                    }
                }
                if (i >= source.size() || source[i].type != TokenType::RightParen) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' after constructor parameters");
                }
                i++; // съели ')'

                if (i >= source.size() || source[i].type != TokenType::LeftBrace) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in constructor body");
                }
                i++; // съели '{'

                auto body = parseBlock();
                if (!body) { 
                    delete node; return std::unexpected(body.error()); 
                }

                auto* ctor = new FuncDecl();
                ctor->line = ln; ctor->column = col;
                ctor->returnType = "void";
                ctor->name = node->name;
                ctor->params = params;
                ctor->body = dynamic_cast<Block*>(*body);
                node->constructor = ctor;
                continue;
            }

            //  Вложенная структура: struct Name { ... }
            if (source[i].type == TokenType::Struct) {
                auto nested = parseStructDecl();
                if (!nested) { delete node; return std::unexpected(nested.error()); }
                node->nestedStructs.push_back(dynamic_cast<StructDecl*>(*nested));
                continue;
            }

            //  Метод: type name(params) { ... }
            if (i + 2 < source.size() && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Iden) && source[i + 1].type == TokenType::Iden && source[i + 2].type == TokenType::LeftParen) {
                auto method = parseFuncDecl();
                if (!method) {
                    delete node; return std::unexpected(method.error()); 
                }
                node->methods.push_back(dynamic_cast<FuncDecl*>(*method));
                continue;
            }

            //  Поле: type name;
            auto fieldType = parseTypeName();
            if (!fieldType) { 
                delete node; return std::unexpected(fieldType.error());
            }

            if (i >= source.size() || source[i].type != TokenType::Iden) {
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected class field name");
            }
            std::string fieldName = source[i++].lexeme;

            Expr* defaultValue = nullptr;
            if (i < source.size() && source[i].type == TokenType::Equal) {
                i++; // съели '='
                auto initExpr = parseEquasion();
                if (!initExpr) {
                    delete node;
                    return std::unexpected(initExpr.error());
                }
                defaultValue = *initExpr;
            }

            if (i >= source.size() || source[i].type != TokenType::Separator) {
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after class field");
            }
            i++; // съели ';'

            node->fields.push_back({*fieldType, fieldName, defaultValue});
        }

        if (i >= source.size()) {
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in class declaration");
        }
        i++; // съели '}'

        return node;
    }

    std::expected<Stmt*, std::string> parseTypeAlias(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'type'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected alias name");
        }
        std::string alias = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::Equal){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '=' in type alias");
        }
        i++; // съели '='

        if (i >= source.size() || (source[i].type != TokenType::TypeName && source[i].type != TokenType::Iden)){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected type in type alias");
        }
        auto origRes = parseTypeName();
        if (!origRes) return std::unexpected(origRes.error());
        std::string original = *origRes;

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after type alias");
        }
        i++; // съели ';'

        auto *node = new TypeAlias();
        node->line = ln; node->column = col;
        node->alias = alias;
        node->original = original;
        return node;
    }

    std::expected<Stmt*, std::string> parseNamespaceDecl(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'namespace'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected namespace name");
        }

        auto *node = new NamespaceDecl();
        node->line = ln; node->column = col;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in namespace");
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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in namespace");
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

        // смотрим вперёд: type [::Iden]* [суффикс]* name "(" → функция, иначе → переменная.
        if (i < source.size() && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Iden)){
            size_t j = i + 1;
            //  namespace-цепочка: ::Iden
            while (j + 1 < source.size() && source[j].type == TokenType::ColonColon && source[j + 1].type == TokenType::Iden){
                j += 2;
            }
            //  Суффиксы массива произвольной глубины вложенности
            while (j < source.size() && source[j].type == TokenType::LeftBracket){
                int depth = 1;
                j++;
                while (j < source.size() && depth > 0){
                    if (source[j].type == TokenType::LeftBracket) depth++;
                    else if (source[j].type == TokenType::RightBracket) depth--;
                    j++;
                }
            }
            if (j + 1 < source.size() && source[j].type == TokenType::Iden && source[j + 1].type == TokenType::LeftParen){
                return parseFuncDecl();
            }
        }

        return parseVarDecl();
    }

    std::expected<Stmt*, std::string> parseImportDecl(){
        int ln = curLine(); int col = curColumn();
        i++; // съели 'import'

        if (i >= source.size()){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected path after 'import'");
        }

        auto *node = new ImportDecl();
        node->line = ln; node->column = col;

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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '>' in C import");
            }
            i++; // съели '>'
            if (header.empty()){
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: empty path in C import");
            }
            node->path = header;
        }
        else if (source[i].type == TokenType::StringLit){
            node->path = source[i++].lexeme;
        }
        else {
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected module path after 'import'");
        }

        //  ';' после import опционален (грамматика его не требует)
        if (i < source.size() && source[i].type == TokenType::Separator){
            i++;
        }

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
            node->line = curLine(); node->column = curColumn();
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
        // пользовательский тип: Iden [::Iden]* [суффиксы]* Iden на верхнем уровне
        if (i < source.size() && source[i].type == TokenType::Iden && looksLikeTypedDecl(i)){
            return parseVarOrFuncDecl();
        }
        return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected top-level declaration, got '" + source[i].lexeme + "'");
    }

    std::expected<Stmt*, std::string> parseVarDecl(){
        auto node = new VarDecl();
        node->line = curLine(); node->column = curColumn();

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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected identifier in variable declaration");
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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after variable declaration");
        }
        i++;

        return node;
    }

    std::expected<Block*, std::string> parseBlock(){
        auto block = new Block;
        block->line = curLine(); block->column = curColumn();

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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}'");
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
            node->line = result->line; node->column = result->column;
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
            node->line = result->line; node->column = result->column;
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

            Operand op;
            if (source[i].type == TokenType::EqualEqual) 
                op = Operand::EqualEqual;
            else
                op = Operand::NotEqual;
            i++;

            auto right = parseComparison();
            if (!right){
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line; node->column = result->column;
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

        while (i < source.size() && (source[i].type == TokenType::Less || source[i].type == TokenType::LessEqual || source[i].type == TokenType::Greater || source[i].type == TokenType::GreaterEqual)){

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
            node->line = result->line; node->column = result->column;
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
            Operand op;
            if (source[i].type == TokenType::Plus) op = Operand::Add;
            else                                   op = Operand::Sub;
            i++;
            auto right = parseMulDiv();
            
            if (!right) {
                return std::unexpected(right.error());
            }
            
            auto *node = new Binary();
            node->line = result->line; node->column = result->column;
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
            node->line = result->line; node->column = result->column;
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
                int ln = curLine(); int col = curColumn();
                i++;
                auto right = parseUnary();

                if (!right){
                    return std::unexpected(right.error());
                }

                auto *node = new Unary();
                node->line = ln; node->column = col;
                node->op = Operand::UnaryMinus;
                node->operand = *right;
                return node;
            }
            if (source[i].type == TokenType::Plus){
                i++;
                return parseUnary();
            }
            if (source[i].type == TokenType::Not){
                int ln = curLine(); int col = curColumn();
                i++;
                auto right = parseUnary();

                if (!right){
                    return std::unexpected(right.error());
                }

                auto *node = new Unary();
                node->line = ln; node->column = col;
                node->op = Operand::Not;
                node->operand = *right;
                return node;
            }
            if (source[i].type == TokenType::PlusPlus || source[i].type == TokenType::MinusMinus){
                int ln = curLine(); int col = curColumn();
                Operand op = (source[i].type == TokenType::PlusPlus) ? Operand::Increment : Operand::Decrement;
                i++;
                auto right = parseUnary();
                if (!right){
                    return std::unexpected(right.error());
                }
                auto *node = new Unary();
                node->line = ln; node->column = col;
                node->op = op;
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
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name after '.'");
                }

                auto *node = new FieldAccess();
                node->line = result->line; node->column = result->column;
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
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ']' after index");
                }
                i++; // съели ']'

                auto *node = new ArrayAccess();
                node->line = result->line; node->column = result->column;
                node->object = result;
                node->index = *index;
                result = node;

            } 
            else if (source[i].type == TokenType::LeftParen){
                i++; // съели '('

                auto *node = new FuncCall();
                node->line = result->line; node->column = result->column;
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
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' after call arguments");
                }
                i++; // съели ')'

                result = node;
            }
            else if (source[i].type == TokenType::PlusPlus){
                i++; // съели '++'

                auto *node = new Unary();
                node->line = result->line; node->column = result->column;
                node->op = Operand::Increment;
                node->operand = result;
                result = node;
                break; // постфиксный инкремент — конец цепочки
            }
            else if (source[i].type == TokenType::MinusMinus){
                i++; // съели '--'

                auto *node = new Unary();
                node->line = result->line; node->column = result->column;
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
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: unexpected end of file");
        }

        // числовой литерал
        if (source[i].type == TokenType::Number){
            auto *node = new Number();
            node->line = curLine(); node->column = curColumn();

            if(source[i].lexeme.find(".") != std::string::npos){
                node->isFloat = true;
            }

            try {
                node->value = std::stod(source[i].lexeme);
            } catch (const std::exception&) {
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: invalid numeric literal '" + source[i].lexeme + "'");
            }
            ++i;
            return node;
        }

        // строковый литерал
        if (source[i].type == TokenType::StringLit){
            auto *node = new String();
            node->line = curLine(); node->column = curColumn();
            node->value = source[i++].lexeme;
            return node;
        }

        // булев литерал
        if (source[i].type == TokenType::BoolLit){
            auto *node = new Bool();
            node->line = curLine(); node->column = curColumn();
            node->value = (source[i++].lexeme == "true");
            return node;
        }

        // символьный литерал
        if (source[i].type == TokenType::CharLit){
            auto *node = new CharLit();
            node->line = curLine(); node->column = curColumn();
            node->value = source[i].lexeme.empty() ? '\0' : source[i].lexeme[0];
            i++;
            return node;
        }

        // null
        if (source[i].type == TokenType::Null){
            auto *node = new NullLit();
            node->line = curLine(); node->column = curColumn();
            i++;
            return node;
        }

        // cast<type>(expr)
        if (source[i].type == TokenType::Cast){
            int ln = curLine(); int col = curColumn();
            i++; // съели 'cast'

            if (i >= source.size() || source[i].type != TokenType::Less){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '<' after cast");
            }
            i++; // съели '<'

            if (i >= source.size() || source[i].type != TokenType::TypeName){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected type in cast<>");
            }
            std::string targetType = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::Greater){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '>' in cast<>");
            }
            i++; // съели '>'

            if (i >= source.size() || source[i].type != TokenType::LeftParen){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after cast<type>");
            }
            i++; // съели '('

            auto expr = parseEquasion();
            if (!expr){
                return std::unexpected(expr.error());
            }

            if (i >= source.size() || source[i].type != TokenType::RightParen){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' in cast");
            }
            i++; // съели ')'

            auto *node = new CastExpr();
            node->line = ln; node->column = col;
            node->targetType = targetType;
            node->value = *expr;
            return node;
        }

        // new ClassName(args...)
        if (source[i].type == TokenType::New) {
            int ln = curLine(); int col = curColumn();
            i++; // съели 'new'

            if (i >= source.size() || source[i].type != TokenType::Iden)
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected class name after 'new'");
            auto* node = new NewExpr();
            node->line = ln; node->column = col;
            node->className = source[i++].lexeme;

            if (i >= source.size() || source[i].type != TokenType::LeftParen) {
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '(' after class name in new");
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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')' in new");
            }
            i++; // съели ')'
            return node;
        }

        // идентификатор, namespace access, 
        if (source[i].type == TokenType::Iden){
            int ln = curLine(); int col = curColumn();
            std::string name = source[i++].lexeme;

            // iden "::" iden {"::" iden}* — доступ к namespace или namespace-qualified литерал/идентификатор
            if (i < source.size() && source[i].type == TokenType::ColonColon){
                std::string qualified = name;
                while (i + 1 < source.size() && source[i].type == TokenType::ColonColon && source[i + 1].type == TokenType::Iden){
                    i++; // съели '::'
                    qualified += "::" + source[i++].lexeme;
                }

                //  namespaced литерал структуры: Math::Vector2D { ... }
                if (i < source.size() && source[i].type == TokenType::LeftBrace){
                    i++; // съели '{'
                    auto *node = new StructLiteral();
                    node->line = ln; node->column = col;
                    node->name = qualified;

                    if (i < source.size() && source[i].type != TokenType::RightBrace){
                        while (true){
                            if (i >= source.size() || source[i].type != TokenType::Iden){
                                delete node;
                                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name in struct literal");
                            }
                            std::string fn = source[i++].lexeme;
                            if (i >= source.size() || source[i].type != TokenType::Colon){
                                delete node;
                                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ':' after field name");
                            }
                            i++;
                            auto val = parseEquasion();
                            if (!val){ delete node; return std::unexpected(val.error()); }
                            node->fields.push_back({fn, *val});
                            if (i < source.size() && source[i].type == TokenType::Comma) i++;
                            else break;
                        }
                    }
                    if (i >= source.size() || source[i].type != TokenType::RightBrace){
                        delete node;
                        return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in struct literal");
                    }
                    i++; // съели '}'
                    return node;
                }

                //  NamespaceAccess: разделяем qualified на nameSpace и member (последний сегмент — member)
                auto pos = qualified.rfind("::");
                auto *node = new NamespaceAccess();
                node->line = ln; node->column = col;
                node->nameSpace = qualified.substr(0, pos);
                node->member = qualified.substr(pos + 2);
                return node;
            }

            // iden "{" field_init_list "}" — литерал структуры
            if (i < source.size() && source[i].type == TokenType::LeftBrace){
                i++; // съели '{'

                auto *node = new StructLiteral();
                node->line = ln; node->column = col;
                node->name = name;

                if (i < source.size() && source[i].type != TokenType::RightBrace){
                    while (true){
                        if (i >= source.size() || source[i].type != TokenType::Iden){
                            delete node;
                            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name in struct literal");
                        }
                        std::string fieldName = source[i++].lexeme;

                        if (i >= source.size() || source[i].type != TokenType::Colon){
                            delete node;
                            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ':' after field name");
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
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in struct literal");
                }
                i++; // съели '}'

                return node;
            }

            // просто идентификатор
            auto *node = new Identifier();
            node->line = ln; node->column = col;
            node->name = name;
            return node;
        }

        // "[" expr_list "]" — литерал массива
        if (source[i].type == TokenType::LeftBracket){
            int ln = curLine(); int col = curColumn();
            i++; // съели '['

            auto *node = new ArrayLiteral();
            node->line = ln; node->column = col;

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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ']' in array literal");
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
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ')'");
            }
            i++; // съели ')'
            return *node;
        }

        return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: unexpected token '" + source[i].lexeme + "'");
    }
};

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source, const std::string& filePath){
    Parser head(source, filePath);
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
