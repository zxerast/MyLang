#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Ast.hpp"
#include "SymbolTable.hpp"
#include "CodeGen.hpp"

int main(int argc, char* argv[]){
    if (argc < 2){
        std::cout << "Usage: ./lang filename.lang\n";
        return 1;
    }

    std::ifstream code_file(argv[1]);
    std::stringstream buf;

    buf << code_file.rdbuf();

    std::string source = buf.str();

    auto tokens = tokenize(source);
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
    auto result = analyzer.analyze(&program, argv[1]);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    //  Кодогенерация — имя исполняемого файла = имя исходника без расширения
    std::string outPath = std::filesystem::path(argv[1]).stem().string();
    CodeGen codegen;
    auto codegenResult = codegen.generate(&program, outPath);
    if (!codegenResult) {
        std::cerr << codegenResult.error() << "\n";
        return 1;
    }

    return 0;
}
