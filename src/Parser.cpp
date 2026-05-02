#include "Tokens.hpp"
#include "Ast.hpp"
#include <vector>
#include <expected>

struct Parser {
    const std::vector<Token>& source;
    const std::string filePath;
    size_t i = 0;

    Parser(const std::vector<Token>& source, const std::string& filePath) : source(source), filePath(filePath) {}

    int curLine() const {
        if (i < source.size()) return source[i].line;
        return 0;
    }

    int curColumn() const {
        if (i < source.size()) return source[i].column;
        return 0;
    }

    // Разбирает имя типа:
    // int, Point, int[], int[3], int[size], int[size + 1], int[][], int[3][4]
    std::expected<TypeName*, std::string> parseTypeName() {
        if (i >= source.size() || (source[i].type != TokenType::TypeName && source[i].type != TokenType::Iden)) {
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected type name");
        }

        auto* type = new TypeName();
        type->base = source[i++].lexeme;

        while (i < source.size() && source[i].type == TokenType::ColonColon) {
            i++; // съели '::'

            if (i >= source.size() || source[i].type != TokenType::Iden) {
                delete type;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected identifier after '::' in type name");
            }

            type->base += "::";
            type->base += source[i++].lexeme;
        }

        while (i < source.size() && source[i].type == TokenType::LeftBracket) {
            i++; // съели '['

            TypeSuffix suffix;

            // T[] — динамический массив
            if (i < source.size() && source[i].type == TokenType::RightBracket) {
                i++; // съели ']'
                suffix.isDynamic = true;
                suffix.size = nullptr;
                type->suffixes.push_back(suffix);
                continue;
            }

            // T[expr] — фиксированный массив
            suffix.isDynamic = false;

            auto sizeExpr = parseEquasion();
            if (!sizeExpr) {
                delete type;
                return std::unexpected(sizeExpr.error());
            }

            suffix.size = *sizeExpr;

            if (i >= source.size() || source[i].type != TokenType::RightBracket) {
                delete type;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ']' after array size");
            }

            i++; // съели ']'

            type->suffixes.push_back(suffix);
        }

        return type;
    } 

    std::expected<TypeName*, std::string> parseValueType() {    //  Прокладка для блокировки типа void для всего кроме функций
        auto typeName = parseTypeName();            //  Функции напрямую вызывают TypeName а всё остальное через блокиратор войда

        if (!typeName) {
            return std::unexpected(typeName.error());
        }

        if ((*typeName)->base == "void") {
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: variable, field or parameter cannot have type 'void'");
        }

        return *typeName;
    }

    bool isAssignOperator(TokenType type) const {
        return type == TokenType::Equal
            || type == TokenType::PlusEqual
            || type == TokenType::MinusEqual
            || type == TokenType::MulEqual
            || type == TokenType::DivideEqual
            || type == TokenType::ModuloEqual;
    }

    AssignOp tokenToAssignOp(TokenType type) const {
        switch (type) {
            case TokenType::Equal:
                return AssignOp::Assign;
            case TokenType::PlusEqual:
                return AssignOp::AddAssign;
            case TokenType::MinusEqual:
                return AssignOp::SubAssign;
            case TokenType::MulEqual:
                return AssignOp::MulAssign;
            case TokenType::DivideEqual:
                return AssignOp::DivAssign;
            case TokenType::ModuloEqual:
                return AssignOp::ModAssign;
            default:
                return AssignOp::Assign;
        }
    }

    bool isLValueSyntax(Expr* expr) const {
        if (dynamic_cast<Identifier*>(expr)) {
            return true;
        }

        if (auto* field = dynamic_cast<FieldAccess*>(expr)) {
            return isLValueSyntax(field->object);
        }

        if (auto* arr = dynamic_cast<ArrayAccess*>(expr)) {
            return isLValueSyntax(arr->object);
        }

        if (dynamic_cast<NamespaceAccess*>(expr)) {
            return true;
        }

        return false;
    }

    bool startsVarDecl() {
        size_t saved = i;

        if (i < source.size() && source[i].type == TokenType::Const) {
            i++;
        }

        if (i < source.size() && source[i].type == TokenType::Auto) {
            i++;

            bool result = (i < source.size() && (source[i].type == TokenType::Iden || source[i].type == TokenType::LeftBracket));

            i = saved;
            return result;
        }

        auto typeName = parseTypeName();
        if (!typeName) {
            i = saved;
            return false;
        }

        delete *typeName;

        bool result = (i < source.size() && source[i].type == TokenType::Iden);

        i = saved;
        return result;
    }

    std::expected<Stmt*, std::string> parseStatement() {
        if (i < source.size() && source[i].type == TokenType::If){
            return parseIf();
        }

        if (i < source.size() && source[i].type == TokenType::While){
            return parseWhile();
        }

        if (startsVarDecl()) {
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

        if (i < source.size() && (source[i].type == TokenType::LeftBrace)){
            i++;
            return parseBlock();
        }

                auto expr = parseLogicOr();

                if (!expr) {
                    return std::unexpected(expr.error());
                }

                // присваивание: lvalue = expr ;
                // составное присваивание: lvalue += expr; -= *= /= %=
                if (i < source.size() && isAssignOperator(source[i].type)) {
                    if (!isLValueSyntax(*expr)) {
                        return std::unexpected(filePath + ":" + std::to_string((*expr)->line) + ":" + std::to_string((*expr)->column) + ": error: left side of assignment must be an lvalue");
                    }

                    AssignOp assignOp = tokenToAssignOp(source[i].type);

                    i++; // съели '=', '+=', '-=', '*=', '/=', '%='

                    auto rhs = parseEquasion();
                    if (!rhs) {
                        return std::unexpected(rhs.error());
                    }

                    if (i >= source.size() || source[i].type != TokenType::Separator) {
                        return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after assignment" );
                    }
                    i++;

                    auto* node = new Assign;
                    node->line = (*expr)->line;
                    node->column = (*expr)->column;
                    node->op = assignOp;
                    node->target = *expr;
                    node->value = *rhs;

                    return node;
                }

        // выражение как инструкция: expr ;
        if (i >= source.size() || source[i].type != TokenType::Separator) {
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ';' after expression");
        }
        i++;

        auto* node = new ExprStmt;
        node->line = (*expr)->line;
        node->column = (*expr)->column;
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
            } 
            else {
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

    std::expected<Param, std::string> parseParam(){
        Param param;

        // const
        if (i < source.size() && source[i].type == TokenType::Const){
            param.isConst = true;
            i++;
        }

        // auto или обычный тип
        if (i < source.size() && source[i].type == TokenType::Auto){
            param.isAuto = true;
            i++;
        }
        else {
            auto paramType = parseValueType();
            if (!paramType){
                return std::unexpected(paramType.error());
            }
            param.typeName = *paramType;
        }

        // имя параметра
        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(
                filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) +
                ": error: expected parameter name"
            );
        }

        param.name = source[i++].lexeme;

        // default value
        if (i < source.size() && source[i].type == TokenType::Equal){
            i++; // съели '='

            auto expr = parseEquasion();
            if (!expr){
                return std::unexpected(expr.error());
            }

            param.defaultValue = *expr;
        }

        // auto обязан иметь default value
        if (param.isAuto && param.defaultValue == nullptr){
            return std::unexpected(
                filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) +
                ": error: auto parameter requires default value"
            );
        }

        return param;
    }

    std::expected<std::vector<Param>, std::string> parseParamList(){
        std::vector<Param> params;

        if (i < source.size() && source[i].type == TokenType::RightParen){
            return params;
        }

        while (true){
            auto param = parseParam();
            if (!param){
                return std::unexpected(param.error());
            }

            params.push_back(*param);

            if (i < source.size() && source[i].type == TokenType::Comma){
                i++;

                if (i < source.size() && source[i].type == TokenType::RightParen){
                    return std::unexpected(
                        filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) +
                        ": error: expected parameter after ','"
                    );
                }

                continue;
            }

            break;
        }

        return params;
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

        auto params = parseParamList();
        if (!params){
            delete node;
            return std::unexpected(params.error());
        }
        node->params = *params;

        if (i >= source.size() || source[i].type != TokenType::RightParen){
            delete node;
            return std::unexpected(
                filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) +
                ": error: expected ')' after function parameters"
            );
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

    std::expected<std::vector<StructField>, std::string> parseFieldDecls() {
        std::vector<StructField> fields;

        bool isConst = false;

        if (i < source.size() && source[i].type == TokenType::Const) {
            i++;
            isConst = true;
        }

        if (i < source.size() && source[i].type == TokenType::Auto) {
            i++;

            while (true) {
                StructField field;
                field.isConst = isConst;
                field.isAuto = true;
                field.typeName = nullptr;

                if (i >= source.size() || source[i].type != TokenType::Iden) {
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name after auto");
                }

                field.name = source[i++].lexeme;

                if (i >= source.size() || source[i].type != TokenType::Equal) {
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: auto field requires initializer");
                }

                i++;

                auto init = parseEquasion();
                if (!init) {
                    return std::unexpected(init.error());
                }

                field.defaultValue = *init;
                fields.push_back(field);

                if (i < source.size() && source[i].type == TokenType::Comma) {
                    i++;
                    continue;
                }

                break;
            }
        }
        else {
            auto fieldType = parseValueType();

            if (!fieldType) {
                return std::unexpected(fieldType.error());
            }

            while (true) {
                StructField field;
                field.isConst = isConst;
                field.isAuto = false;

                auto* dst = new TypeName();
                dst->base = (*fieldType)->base;
                dst->suffixes = (*fieldType)->suffixes;

                field.typeName = dst;

                if (i >= source.size() || source[i].type != TokenType::Iden) {
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name");
                }

                field.name = source[i++].lexeme;

                if (i < source.size() && source[i].type == TokenType::Equal) {
                    i++;

                    auto init = parseEquasion();
                    if (!init) {
                        return std::unexpected(init.error());
                    }

                    field.defaultValue = *init;
                }

                fields.push_back(field);

                if (i < source.size() && source[i].type == TokenType::Comma) {
                    i++;
                    continue;
                }

                break;
            }
        }

        if (i >= source.size() || source[i].type != TokenType::Separator) {
            return std::unexpected(
                filePath + ":" + std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected ';' after field declaration"
            );
        }

        i++;
        return fields;
    } 

    std::expected<Stmt*, std::string> parseStructDecl(){
        int ln = curLine(); 
        int col = curColumn();

        i++; // съели 'struct'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected struct name");
        }

        auto *node = new StructDecl();
        node->line = ln; 
        node->column = col;
        node->name = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::LeftBrace){
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '{' in struct declaration");
        }

        i++; // съели '{'

        while (i < source.size() && source[i].type != TokenType::RightBrace){
            auto fields = parseFieldDecls();
            if (!fields){
                delete node;
                return std::unexpected(fields.error());
            }

            for (auto& field : *fields) {
                node->fields.push_back(field);
            }
        }

        if (i >= source.size()){
            delete node;
            return std::unexpected(
                filePath + ":" +
                std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected '}' in struct declaration"
            );
        }

        i++; // съели '}'

        return node;
    } 

    bool isFuncMethod() {
        size_t saved = i;

        auto type = parseTypeName();

        if (!type) {
            i = saved;
            return false;
        }

        // parseTypeName() уже выделил TypeName*, но здесь мы только проверяем lookahead
        delete *type;

        bool result = i + 1 < source.size() && source[i].type == TokenType::Iden && source[i + 1].type == TokenType::LeftParen;

        i = saved;
        return result;
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

            // Вложенная структура: struct Name { ... }
            if (source[i].type == TokenType::Struct) {
                auto nestedStruct = parseStructDecl();
                if (!nestedStruct) {
                    delete node;
                    return std::unexpected(nestedStruct.error());
                }

                auto* structNode = dynamic_cast<StructDecl*>(*nestedStruct);
                if (structNode == nullptr) {
                    delete node;
                    delete *nestedStruct;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: internal parser error: expected struct declaration");
                }

                node->structs.push_back(structNode);
                continue;
            }

            //  Деструктор: ~ClassName()
            if (i + 1 < source.size() && source[i].type == TokenType::Tilde
                && source[i + 1].type == TokenType::Iden && source[i + 1].lexeme == node->name) {
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
                dtor->line = ln; 
                dtor->column = col;

                auto* voidType = new TypeName();
                voidType->base = "void";
                dtor->returnType = voidType;

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
                auto params = parseParamList();
                if (!params){
                    delete node;
                    return std::unexpected(params.error());
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
                if (!body) { delete node; return std::unexpected(body.error()); }

                auto* ctor = new FuncDecl();
                ctor->line = ln; 
                ctor->column = col;
                
                auto* voidType = new TypeName();
                voidType->base = "void";
                ctor->returnType = voidType;

                ctor->name = node->name;
                ctor->params = *params;
                ctor->body = dynamic_cast<Block*>(*body);
                node->constructor = ctor;
                continue;
            }

            //  Метод: type name(params) { ... }
            if (isFuncMethod()) {
                auto method = parseFuncDecl();

                if (!method) {
                    delete node;
                    return std::unexpected(method.error());
                }

                auto* methodNode = dynamic_cast<FuncDecl*>(*method);
                if (methodNode == nullptr) {
                    delete node;
                    delete *method;
                    return std::unexpected(
                        filePath + ":" +
                        std::to_string(curLine()) + ":" +
                        std::to_string(curColumn()) +
                        ": error: internal parser error: expected function declaration"
                    );
                }

                node->methods.push_back(methodNode);
                continue;
            }

            //  Поле: type name; / type name = expr; / auto name = expr;
            auto fields = parseFieldDecls();
            if (!fields){
                delete node;
                return std::unexpected(fields.error());
            }

            for (auto& field : *fields) {
                node->fields.push_back(field);
            }
        }

        if (i >= source.size()) {
            delete node;
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in class declaration");
        }
        i++; // съели '}'

        return node;
    }

    std::expected<Stmt*, std::string> parseTypeAlias(){
        int ln = curLine(); 
        int col = curColumn();

        i++; // съели 'type'

        if (i >= source.size() || source[i].type != TokenType::Iden){
            return std::unexpected(
                filePath + ":" +
                std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected alias name"
            );
        }

        std::string alias = source[i++].lexeme;

        if (i >= source.size() || source[i].type != TokenType::Equal){
            return std::unexpected(
                filePath + ":" +
                std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected '=' in type alias"
            );
        }

        i++; // съели '='

        auto originalType = parseTypeName();
        if (!originalType){
            return std::unexpected(originalType.error());
        }

        if (i >= source.size() || source[i].type != TokenType::Separator){
            return std::unexpected(
                filePath + ":" +
                std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected ';' after type alias"
            );
        }

        i++; // съели ';'

        auto *node = new TypeAlias();
        node->line = ln; 
        node->column = col;
        node->alias = alias;
        node->original = *originalType;

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
        // const, auto → всегда переменная
        if (i < source.size() && (source[i].type == TokenType::Const || source[i].type == TokenType::Auto)){
            return parseVarDecl();
        }

        size_t saved = i;

        auto typeName = parseTypeName();

        if (!typeName){
            i = saved;
            return parseVarDecl();
        }

        // После полного типа должна идти конструкция:
        // name "("
        // Тогда это функция.
        if (i + 1 < source.size() && source[i].type == TokenType::Iden && source[i + 1].type == TokenType::LeftParen){
            i = saved;
            return parseFuncDecl();
        }

        // Иначе это объявление переменной.
        i = saved;
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

        return node;
    }

    std::expected<Stmt*, std::string> parseTopDecl(){
        // export обёртка
        if (i < source.size() && source[i].type == TokenType::Import){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: import must appear before all declarations"
            );
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
        // var_decl или func_decl — оба начинаются с [const] type iden
        if (i < source.size() && (source[i].type == TokenType::TypeName || source[i].type == TokenType::Const || source[i].type == TokenType::Iden || source[i].type == TokenType::Auto)){
            return parseVarOrFuncDecl();
        }
        return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected top-level declaration, got '" + source[i].lexeme + "'");
    }

    std::expected<Stmt*, std::string> parseVarDecl(){
        auto node = new VarDecl();
        node->line = curLine(); 
        node->column = curColumn();

        if (i < source.size() && source[i].type == TokenType::Const){
            i++;
            node->isConst = true;
        }

        if (i < source.size() && source[i].type == TokenType::Auto){
            i++;
            node->typeName = nullptr;
            node->isAuto = true;
            if (i < source.size() && source[i].type == TokenType::LeftBracket){
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: 'auto' cannot have array suffix; use 'T[] name' or 'auto name = [ ... ]'");
            }
        }
        else {
            auto typeName = parseValueType();
            if (!typeName) {
                delete node;
                return std::unexpected(typeName.error());
            }
            node->typeName = *typeName;
        }

        // список переменных: name [= expr] {, name [= expr]}
        // но для auto: name = expr {, name = expr}
        while (true) {
            if (i >= source.size() || source[i].type != TokenType::Iden) {
                delete node;
                return std::unexpected(
                    filePath + ":" +
                    std::to_string(curLine()) + ":" +
                    std::to_string(curColumn()) +
                    ": error: expected identifier in variable declaration"
                );
            }

            auto var = new VarInit();
            var->name = source[i++].lexeme;

            if (i < source.size() && source[i].type == TokenType::Equal) {
                i++; // съели '='

                auto init = parseEquasion();
                if (!init) {
                    delete var;
                    delete node;
                    return std::unexpected(init.error());
                }

                var->init = *init;
            }
            else if (node->isAuto) {
                delete var;
                delete node;
                return std::unexpected(
                    filePath + ":" +
                    std::to_string(curLine()) + ":" +
                    std::to_string(curColumn()) +
                    ": error: auto variable requires initializer"
                );
            }

            node->vars.push_back(var);

            if (i >= source.size() || source[i].type != TokenType::Comma) {
                break;
            }

            i++; // съели ','
        }

        if (i >= source.size() || source[i].type != TokenType::Separator) {
            delete node;
            return std::unexpected(
                filePath + ":" +
                std::to_string(curLine()) + ":" +
                std::to_string(curColumn()) +
                ": error: expected ';' after variable declaration"
            );
        }

        i++; // съели ';'

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
            if (source[i].type == TokenType::EqualEqual) op = Operand::EqualEqual;
            else                                         op = Operand::NotEqual;
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
        auto left = parsePower();

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
            auto right = parsePower();
            
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

    std::expected<Expr*, std::string> parsePower() {
        auto left = parseUnary();

        if (!left){
            return std::unexpected(left.error());
        }

        Expr* result = *left;

        if (i < source.size() && source[i].type == TokenType::Caret) {
            i++; // съели '^'

            auto right = parsePower();
            if (!right) {
                return std::unexpected(right.error());
            }

            auto *node = new Binary();
            node->line = result->line;
            node->column = result->column;
            node->op = Operand::Pow;
            node->left = result;
            node->right = *right;

            return node;
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

    std::string joinQualifiedPrefix(const std::vector<std::string>& parts, size_t endExclusive) {
        std::string result;

        for (size_t j = 0; j < endExclusive; ++j) {
            if (j > 0) {
                result += "::";
            }
            result += parts[j];
        }

        return result;
    }

    std::expected<std::vector<std::string>, std::string> parseQualifiedName() {
        std::vector<std::string> parts;

        if (i >= source.size() || source[i].type != TokenType::Iden) {
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected identifier");
        }

        parts.push_back(source[i++].lexeme);

        while (i < source.size() && source[i].type == TokenType::ColonColon) {
            i++; // съели '::'

            if (i >= source.size() || source[i].type != TokenType::Iden) {
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected identifier after '::'");
            }

            parts.push_back(source[i++].lexeme);
        }

        return parts;
    }

    std::expected<Expr*, std::string> parsePrimary() {
        if (i >= source.size()){
            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: unexpected end of file");
        }

        // числовой литерал
        if (source[i].type == TokenType::Number){
            auto *node = new Number();
            node->line = curLine(); node->column = curColumn();
            node->isFloat = source[i].isFloat;
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

        // null-литерал
        if (source[i].type == TokenType::Null) {
            auto *node = new NullLiteral();
            node->line = curLine();
            node->column = curColumn();
            i++; // съели 'null'
            return node;
        }

        // символьный литерал
        if (source[i].type == TokenType::CharLit) {
            auto *node = new CharLiteral();
            node->line = curLine();
            node->column = curColumn();

            std::string lex = source[i].lexeme;

            // Если лексер хранит char как 'a' или "a"
            if (lex.size() == 3 && (lex[0] == '\'' || lex[0] == '"') && lex[2] == lex[0]) {
                node->value = lex[1];
            }
            // Если лексер хранит только сам символ: a
            else if (lex.size() == 1) {
                node->value = lex[0];
            }
            else {
                delete node;
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: invalid char literal '" + lex + "'");
            }

            i++; // съели char literal
            return node;
        }

       // cast<type>(expr)
        if (source[i].type == TokenType::Cast){
            int ln = curLine(); 
            int col = curColumn();

            i++; // съели 'cast'

            if (i >= source.size() || source[i].type != TokenType::Less){
                return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '<' after cast");
            }

            i++; // съели '<'

            auto targetType = parseTypeName();
            if (!targetType){
                return std::unexpected(targetType.error());
            }

            if (i >= source.size() || source[i].type != TokenType::Greater){
                return std::unexpected(
                    filePath + ":" +
                    std::to_string(curLine()) + ":" +
                    std::to_string(curColumn()) +
                    ": error: expected '>' in cast<>"
                );
            }

            i++; // съели '>'

            if (i >= source.size() || source[i].type != TokenType::LeftParen){
                return std::unexpected(
                    filePath + ":" +
                    std::to_string(curLine()) + ":" +
                    std::to_string(curColumn()) +
                    ": error: expected '(' after cast<type>"
                );
            }

            i++; // съели '('

            auto expr = parseEquasion();
            if (!expr){
                return std::unexpected(expr.error());
            }

            if (i >= source.size() || source[i].type != TokenType::RightParen){
                return std::unexpected(
                    filePath + ":" +
                    std::to_string(curLine()) + ":" +
                    std::to_string(curColumn()) +
                    ": error: expected ')' in cast"
                );
            }

            i++; // съели ')'

            auto *node = new CastExpr();
            node->line = ln; 
            node->column = col;
            node->targetType = *targetType;
            node->value = *expr;

            return node;
        } 

        

       // идентификатор, namespace access, struct literal
        if (source[i].type == TokenType::Iden) {
            int ln = curLine();
            int col = curColumn();

            auto qualifiedName = parseQualifiedName();
            if (!qualifiedName) {
                return std::unexpected(qualifiedName.error());
            }

            std::vector<std::string> parts = *qualifiedName;

            // iden {"::" iden} "{" field_init_list "}"
            // Примеры:
            // Point { x: 1 }
            // Math::Point { x: 1 }
            if (i < source.size() && source[i].type == TokenType::LeftBrace) {
                i++; // съели '{'

                auto* node = new StructLiteral();
                node->line = ln;
                node->column = col;
                node->name = joinQualifiedPrefix(parts, parts.size());

                if (i < source.size() && source[i].type != TokenType::RightBrace) {
                    while (true) {
                        if (i >= source.size() || source[i].type != TokenType::Iden) {
                            delete node;
                            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected field name in struct literal");
                        }

                        std::string fieldName = source[i++].lexeme;

                        if (i >= source.size() || source[i].type != TokenType::Colon) {
                            delete node;
                            return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected ':' after field name");
                        }
                        i++; // съели ':'

                        auto val = parseEquasion();
                        if (!val) {
                            delete node;
                            return std::unexpected(val.error());
                        }

                        node->fields.push_back({fieldName, *val});

                        if (i < source.size() && source[i].type == TokenType::Comma) {
                            i++; // съели ','
                        } else {
                            break;
                        }
                    }
                }

                if (i >= source.size() || source[i].type != TokenType::RightBrace) {
                    delete node;
                    return std::unexpected(filePath + ":" + std::to_string(curLine()) + ":" + std::to_string(curColumn()) + ": error: expected '}' in struct literal");
                }
                i++; // съели '}'

                return node;
            }

            // Простой идентификатор:
            // x
            if (parts.size() == 1) {
                auto* node = new Identifier();
                node->line = ln;
                node->column = col;
                node->name = parts[0];
                return node;
            }

            // Namespace access:
            // Math::PI_INT
            // Math::Add::PI_FLOAT
            auto* node = new NamespaceAccess();
            node->line = ln;
            node->column = col;
            node->nameSpace = joinQualifiedPrefix(parts, parts.size() - 1);
            node->member = parts.back();
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

std::expected<std::vector<Stmt*>, std::string> parse(const std::vector<Token>& source, const std::string& filePath) {
    Parser head(source, filePath);
    std::vector<Stmt*> declarations;

    // 1. Сначала парсим ВСЕ import
    while (head.i < source.size() && source[head.i].type == TokenType::Import) {

        auto decl = head.parseImportDecl();
        if (!decl) {
            return std::unexpected(decl.error());
        }

        declarations.push_back(*decl);
    }

    // 2. Затем обычные top_decl
    while (head.i < source.size() && source[head.i].type != TokenType::End) {

        // запрет import не в начале
        if (source[head.i].type == TokenType::Import) {
            return std::unexpected(filePath + ":" + std::to_string(head.curLine()) + ":" + std::to_string(head.curColumn()) + ": error: import must appear before all declarations");
        }

        auto decl = head.parseTopDecl();
        if (!decl) {
            return std::unexpected(decl.error());
        }

        declarations.push_back(*decl);
    }

    return declarations;
}
