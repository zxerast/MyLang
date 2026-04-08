#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include "Ast.hpp"
#include "SymbolTable.hpp"

int main(int argc, char* argv[]){
    if (argc < 2){
        std::cout << "Usage: ./lang filename.lang\n";
        return 1;
    }

    std::ifstream code_file(argv[1]);
    std::stringstream buf;

    buf << code_file.rdbuf();

    std::string s = buf.str();

    auto tokens = tokenize(s);
    if (!tokens) {
        std::cerr << tokens.error() << "\n";
        return 1;
    }

    auto nodes = parse(*tokens);
    if (!nodes) {
        std::cerr << nodes.error() << "\n";
        return 1;
    }

    //  Собираем Program из результата парсера
    Program program;
    program.decls = *nodes;

    //  Семантический анализ
    SemanticAnalyzer analyzer;
    auto result = analyzer.analyze(&program);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    return 0;
}
