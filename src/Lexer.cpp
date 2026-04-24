#include "Tokens.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <expected>

Token typeIdentifier(const std::string& elem){
    if (elem == "int"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "uint"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "char"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "float"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "bool"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "string"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "void"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "int8"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "int16"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "int32"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "int64"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "uint8"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "uint16"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "uint32"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "uint64"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "float32"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "float64"){
        return {TokenType::TypeName, elem};
    }
    else if (elem == "if"){
        return {TokenType::If,   elem};
    }
    else if (elem == "else"){
        return {TokenType::Else,   elem};
    }
    else if (elem == "while"){
        return {TokenType::While,   elem};
    }
    else if (elem == "break"){
        return {TokenType::Break,   elem};
    }
    else if (elem == "const"){
        return {TokenType::Const,   elem};
    }
    else if (elem == "struct"){
        return {TokenType::Struct,   elem};
    }
    else if (elem == "continue"){
        return {TokenType::Continue,   elem};
    }
    else if (elem == "return"){
        return {TokenType::Return,   elem};
    }
    else if (elem == "type"){
        return {TokenType::Type,   elem};
    }
    else if (elem == "namespace"){
        return {TokenType::Namespace,   elem};
    }
    else if (elem == "cast"){
        return {TokenType::Cast,   elem};
    }
    else if (elem == "auto"){
        return {TokenType::Auto,   elem};
    }
    else if (elem == "import"){
        return {TokenType::Import,   elem};
    }
    else if (elem == "export"){
        return {TokenType::Export,   elem};
    }
    else if (elem == "class"){
        return {TokenType::Class,   elem};
    }
    else if (elem == "new"){
        return {TokenType::New, elem};
    }
    else if (elem == "delete"){
        return {TokenType::Delete, elem};
    }
    else if (elem == "null"){
        return {TokenType::Null, elem};
    }
    else if (elem == "true" || elem == "false"){
        return {TokenType::BoolLit,   elem};
    }
    else{
        return {TokenType::Iden,   elem};
    }
}

std::expected<std::vector<Token>, std::string> tokenize(const std::string& source, const std::string& filePath){
    std::vector<Token> res;
    int line = 1;
    size_t lineStart = 0;

    for(size_t i = 0; i < source.size(); i++){
        bool hasDot = false;
        size_t prevSize = res.size();
        int startCol = static_cast<int>(i - lineStart) + 1;
        int startLine = line;
        switch (source[i]){
            case ' ':
            case '\t':
            case '\r': break;
            case '\n': line++; lineStart = i + 1; break;
            case '*':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::MulEqual, std::string("*="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Multiply, std::string(1,  '*'));
                }
                break;
            case '%':
                if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::ModEqual, std::string("%="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Modulo, std::string(1, '%'));
                }
                break;
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
                else if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::PlusEqual, std::string("+="));
                    i++;
                }
                else{
                    res.emplace_back(TokenType::Plus, std::string(1, '+'));
                }
                break;
            case '-':
                if (i + 1 < source.size() && source[i + 1] == '-'){
                    res.emplace_back(TokenType::MinusMinus, std::string("--"));
                    i++;
                }
                else if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::MinusEqual, std::string("-="));
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
                    i--;
                }
                else if (i + 1 < source.size() && source[i + 1] == '='){
                    res.emplace_back(TokenType::DivEqual, std::string("/="));
                    i++;
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
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unexpected character '&'");
                }
                break;
            case '|':
                if (i + 1 < source.size() && source[i + 1] == '|'){
                    res.emplace_back(TokenType::Or, std::string("||"));
                    i++;
                }
                else{
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unexpected character '|'");
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
            case '\'': {
                i++;
                if (i >= source.size() || source[i] == '\n' || source[i] == '\''){
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: empty or unterminated character literal");
                }
                char ch;
                if (source[i] == '\\' && i + 1 < source.size()){
                    char esc = source[i + 1];
                    if      (esc == 'n')  ch = '\n';
                    else if (esc == 't')  ch = '\t';
                    else if (esc == 'r')  ch = '\r';
                    else if (esc == '0')  ch = '\0';
                    else if (esc == '\\') ch = '\\';
                    else if (esc == '"')  ch = '"';
                    else if (esc == '\'') ch = '\'';
                    else {
                        return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unknown escape sequence '\\" + std::string(1, esc) + "'");
                    }
                    i += 2;
                }
                else{
                    ch = source[i++];
                }
                if (i >= source.size() || source[i] != '\''){
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unterminated character literal");
                }
                res.emplace_back(TokenType::CharLit, std::string(1, ch));
                break;
            }
            case '"': {
                std::string str;
                i++;
                while (i < source.size() && source[i] != '"' && source[i] != '\n'){
                    if (source[i] == '\\' && i + 1 < source.size()) {    //  Escape-последовательности
                        char esc = source[i + 1];
                        if      (esc == 'n')  str.push_back('\n');
                        else if (esc == 't')  str.push_back('\t');
                        else if (esc == 'r')  str.push_back('\r');
                        else if (esc == '0')  str.push_back('\0');
                        else if (esc == '\\') str.push_back('\\');
                        else if (esc == '"')  str.push_back('"');
                        else if (esc == '\'') str.push_back('\'');
                        else {
                            return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unknown escape sequence '\\" + std::string(1, esc) + "'");
                        }
                        i += 2;
                        continue;
                    }
                    str.push_back(source[i++]);
                }
                if (i >= source.size() || source[i] != '"'){
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unterminated string literal");
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
                    //  Hex-литерал: 0x...
                    if (source[i] == '0' && i + 1 < source.size() && (source[i + 1] == 'x' || source[i + 1] == 'X')){
                        elem.push_back(source[i++]); // '0'
                        elem.push_back(source[i++]); // 'x'
                        while (i < source.size() && isxdigit(source[i])){
                            elem.push_back(source[i++]);
                        }
                        i--;
                        unsigned long long hexVal = std::stoull(elem.substr(2), nullptr, 16);
                        res.emplace_back(TokenType::Number, std::to_string(hexVal));
                        break;
                    }
                    while (i < source.size() && (isdigit(source[i]) || source[i] == '.')){
                        if (source[i] == '.'){
                            if (!isdigit(source[i - 1])){
                                return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: malformed floating-point literal");
                            }

                            if (hasDot == false){
                                hasDot = true;
                            }
                            else{
                                return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: multiple dots in numeric literal");
                            }
                        }
                        elem.push_back(source[i++]);
                    }
                    i--;
                    res.emplace_back(TokenType::Number, elem);
                }
                else{
                    return std::unexpected(filePath + ":" + std::to_string(startLine) + ":" + std::to_string(startCol) + ": error: unexpected character '" + std::string(1, source[i]) + "'");
                }
                break;
        }
        if (res.size() > prevSize){
            res.back().line = startLine;
            res.back().column = startCol;
        }
    }
    int endCol = static_cast<int>(source.size() - lineStart) + 1;
    res.emplace_back(TokenType::End, std::string(1, ' '));
    res.back().line = line;
    res.back().column = endCol;

    return res;
}
