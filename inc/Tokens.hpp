#pragma once

#include <string>
#include <vector>
#include <expected>

enum class TokenType {   // Тип токенов
    Number,
    StringLit,
    CharLit,
    BoolLit,
    Plus,
    PlusPlus,
    Minus,
    MinusMinus,
    Multiply,
    Divide,
    Modulo,
    Caret,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    TypeName,
    Iden,
    Equal,
    EqualEqual,
    PlusEqual,
    MinusEqual,
    MulEqual,
    DivideEqual,
    ModuloEqual,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    And,
    Or,
    Not,
    Dot,
    Colon,
    ColonColon,
    Comma,
    Separator,
    Const,
    Struct,
    Type,
    Namespace,
    Cast,
    If,
    Else,
    While,
    Break,
    Continue,
    Return,
    Auto,
    Import,
    Export,
    Class,
    Tilde,
    Null,
    End,
};

struct Token {

    TokenType type;  // Один токен типа выше
    std::string lexeme; // Токен в виде строки
    int line = 0;       // Номер строки для отслеживания ошибок
    int column = 0;     // Номер столбца (1-based) начала токена
    bool isFloat = false;

    Token(){
        this->type = TokenType::End;
        this->lexeme = "";
    }

    Token(TokenType type, std::string lexeme){
        this->type = type;
        this->lexeme = lexeme;
    }
};



std::expected<std::vector<Token>, std::string> tokenize(const std::string& source, const std::string& filePath = "<source>");
Token typeIdentifier(const std::string& elem);
