#pragma once

#include <string>
#include <vector>
#include <expected>

enum class TokenType {   // Тип токенов
    Number,
    StringLit,
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
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    And,
    Or,
    Not,
    Dot,
    Arrow,
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
    New,
    Delete,
    Tilde,
    End,
};

enum class SubType {
    None,
    Int,
    Uint,
    Char,
    Float,
    Bool,
    String,
    Void,
    Int8,
    Int16,
    Int32,
    Int64,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
    Float32,
    Float64
};

struct Token {

    TokenType type;  // Один токен типа выше
    SubType subType;
    std::string lexeme; // Токен в виде строки
    int line = 0;       // Номер строки для отслеживания ошибок

    Token(){
        this->type = TokenType::End;
        this->lexeme = "";
        this->subType = SubType::None;
    }

    Token(TokenType type, std::string lexeme){
        this->type = type;
        this->lexeme = lexeme;
        this->subType = SubType::None;

    }

    Token(TokenType type, SubType subType, std::string lexeme){
        this->type = type;
        this->lexeme = lexeme;
        this->subType = subType;
    }
};



std::expected<std::vector<Token>, std::string> tokenize(const std::string& source);
Token typeIdentifier(const std::string& elem);
