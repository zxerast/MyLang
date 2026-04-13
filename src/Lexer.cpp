#include "Tokens.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <expected>

Token typeIdentifier(const std::string& elem){
    if (elem == "int"){
        return {TokenType::TypeName, SubType::Int, elem};
    }
    else if (elem == "uint"){
        return {TokenType::TypeName, SubType::Uint, elem};
    }
    else if (elem == "char"){
        return {TokenType::TypeName, SubType::Char, elem};
    }
    else if (elem == "float"){
        return {TokenType::TypeName, SubType::Float, elem};
    }
    else if (elem == "bool"){
        return {TokenType::TypeName, SubType::Bool, elem};
    }
    else if (elem == "string"){
        return {TokenType::TypeName, SubType::String, elem};
    }
    else if (elem == "void"){
        return {TokenType::TypeName, SubType::Void, elem};
    }
    else if (elem == "int8"){
        return {TokenType::TypeName, SubType::Int8, elem};
    }
    else if (elem == "int16"){
        return {TokenType::TypeName, SubType::Int16, elem};
    }
    else if (elem == "int32"){
        return {TokenType::TypeName, SubType::Int32, elem};
    }
    else if (elem == "int64"){
        return {TokenType::TypeName, SubType::Int64, elem};
    }
    else if (elem == "uint8"){
        return {TokenType::TypeName, SubType::Uint8, elem};
    }
    else if (elem == "uint16"){
        return {TokenType::TypeName, SubType::Uint16, elem};
    }
    else if (elem == "uint32"){
        return {TokenType::TypeName, SubType::Uint32, elem};
    }
    else if (elem == "uint64"){
        return {TokenType::TypeName, SubType::Uint64, elem};
    }
    else if (elem == "float32"){
        return {TokenType::TypeName, SubType::Float32, elem};
    }
    else if (elem == "float64"){
        return {TokenType::TypeName, SubType::Float64, elem};
    }
    else if (elem == "if"){
        return {TokenType::If, SubType::None, elem};
    }
    else if (elem == "else"){
        return {TokenType::Else, SubType::None, elem};
    }
    else if (elem == "while"){
        return {TokenType::While, SubType::None, elem};
    }
    else if (elem == "break"){
        return {TokenType::Break, SubType::None, elem};
    }
    else if (elem == "const"){
        return {TokenType::Const, SubType::None, elem};
    }
    else if (elem == "struct"){
        return {TokenType::Struct, SubType::None, elem};
    }
    else if (elem == "continue"){
        return {TokenType::Continue, SubType::None, elem};
    }
    else if (elem == "return"){
        return {TokenType::Return, SubType::None, elem};
    }
    else if (elem == "type"){
        return {TokenType::Type, SubType::None, elem};
    }
    else if (elem == "namespace"){
        return {TokenType::Namespace, SubType::None, elem};
    }
    else if (elem == "cast"){
        return {TokenType::Cast, SubType::None, elem};
    }
    else if (elem == "auto"){
        return {TokenType::Auto, SubType::None, elem};
    }
    else if (elem == "import"){
        return {TokenType::Import, SubType::None, elem};
    }
    else if (elem == "export"){
        return {TokenType::Export, SubType::None, elem};
    }
    else if (elem == "class"){
        return {TokenType::Class, SubType::None, elem};
    }
    else if (elem == "new"){
        return {TokenType::New, SubType::None, elem};
    }
    else if (elem == "delete"){
        return {TokenType::Delete, SubType::None, elem};
    }
    else if (elem == "true" || elem == "false"){
        return {TokenType::BoolLit, SubType::None, elem};
    }
    else{
        return {TokenType::Iden, SubType::None, elem};
    }
}

std::expected<std::vector<Token>, std::string> tokenize(const std::string& source){
    std::vector<Token> res;
    int line = 1;

    for(size_t i = 0; i < source.size(); i++){
        bool hasDot = false;
        size_t prevSize = res.size();
        switch (source[i]){
            case ' ':
            case '\t':
            case '\r': break;
            case '\n': line++; break;
            case '*': res.emplace_back(TokenType::Multiply, std::string(1,  '*')); break;
            case '%': res.emplace_back(TokenType::Modulo, std::string(1, '%')); break;
            case '(': res.emplace_back(TokenType::LeftParen, std::string(1, '(')); break;
            case ')': res.emplace_back(TokenType::RightParen, std::string(1, ')')); break;
            case '{': res.emplace_back(TokenType::LeftBrace, std::string(1, '{')); break;
            case '}': res.emplace_back(TokenType::RightBrace, std::string(1, '}')); break;
            case '[': res.emplace_back(TokenType::LeftBracket, std::string(1, '[')); break;
            case ']': res.emplace_back(TokenType::RightBracket, std::string(1, ']')); break;
            case ',': res.emplace_back(TokenType::Comma, std::string(1, ',')); break;
            case '.': res.emplace_back(TokenType::Dot, std::string(1, '.')); break;
            case ';': res.emplace_back(TokenType::Separator, std::string(1, ';')); break;
            case '^': res.emplace_back(TokenType::Caret, std::string(1, '^')); break;
            case '+':
                if (i + 1 < source.size() && source[i + 1] == '+'){
                    res.emplace_back(TokenType::PlusPlus, std::string("++"));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Plus, std::string(1, '+'));
                }
                break;
            case '-':
                if (i + 1 < source.size() && source[i + 1] == '>'){
                    res.emplace_back(TokenType::Arrow, std::string("->"));
                    i++;
                }
                else if (i + 1 < source.size() && source[i + 1] == '-'){
                    res.emplace_back(TokenType::MinusMinus, std::string("--"));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Minus, std::string(1, '-'));
                }
                break;
            case '/':
                if (i + 1 < source.size() && source[i + 1] == '/'){
                    while (i < source.size() && source[i] != '\n'){
                        i++;
                    }
                }
                else{
                    res.emplace_back(TokenType::Divide, std::string(1, '/'));
                }
                break;
            case '=':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::EqualEqual, std::string("=="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Equal, std::string(1, '='));
                }
                break;
            case '!':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::NotEqual, std::string("!="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Not, std::string(1, '!'));
                }
                break;
            case '<':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::LessEqual, std::string("<="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Less, std::string(1, '<'));
                }
                break;
            case '>':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::GreaterEqual, std::string(">="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Greater, std::string(1, '>'));
                }
                break;
            case '&':
                if (i + 1 < source.size() && source[i + 1] == '&'){
                    res.emplace_back(TokenType::And, std::string("&&"));
                    i++;
                }
                else{
                    return std::unexpected("Ошибка лексера: неожиданный символ '&'");
                }
                break;
            case '|':
                if (i + 1 < source.size() && source[i + 1] == '|'){
                    res.emplace_back(TokenType::Or, std::string("||"));
                    i++;
                }
                else{
                    return std::unexpected("Ошибка лексера: неожиданный символ '|'");
                }
                break;
            case ':':
                if (i + 1 < source.size() && source[i + 1] == ':'){
                    res.emplace_back(TokenType::ColonColon, std::string("::"));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Colon, std::string(1, ':'));
                }
                break;
            case '~':
                res.emplace_back(TokenType::Tilde, std::string(1, '~'));
                break;
            case '"': {
                std::string str;
                i++;
                while (i < source.size() && source[i] != '"' && source[i] != '\n'){
                    str.push_back(source[i++]);
                }
                if (i >= source.size() || source[i] != '"'){
                    return std::unexpected("Ошибка лексера: незакрытый строковый литерал");
                }
                res.emplace_back(TokenType::StringLit, str);
                break;
            }
            default:
                std::string elem;

                if (isalpha(source[i]) || source[i] == '_'){
                    while (i < source.size() && (isalnum(source[i]) || source[i] == '_')){
                        elem.push_back(source[i++]);
                    }
                    i--;
                    res.emplace_back(typeIdentifier(elem));
                }
                else if (isdigit(source[i])){
                    SubType type = SubType::None;

                    while (i < source.size() && (isdigit(source[i]) || source[i] == '.')){
                        if (source[i] == '.'){
                            type = SubType::Float;
                            if (!isdigit(source[i - 1])){
                                return std::unexpected("Ошибка лексера: некорректный литерал с плавающей точкой");
                            }

                            if (hasDot == false){
                                hasDot = true;
                            }
                            else{
                                return std::unexpected("Ошибка лексера: несколько точек в числовом литерале");
                            }
                        }
                        elem.push_back(source[i++]);
                    }
                    i--;
                    res.emplace_back(TokenType::Number, type, elem);
                }
                else{
                    return std::unexpected(std::string("Ошибка лексера: неожиданный символ '") + source[i] + "'");
                }
                break;
        }
        if (res.size() > prevSize)
            res.back().line = line;
    }
    res.emplace_back(TokenType::End, std::string(1, ' '));
    res.back().line = line;

    return res;
}
