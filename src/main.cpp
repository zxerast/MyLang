#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Ast.hpp"
#include "SymbolTable.hpp"
#include "CodeGen.hpp"

int main(int argc, char* argv[]){
    std::string sourcePath;
    std::string outputPath;
    bool dumpTokens = false;
    // bool dumpAst = false;

    for (int i = 1; i < argc; i++){
        std::string arg = argv[i];
        if (arg == "-o"){
            if (i + 1 >= argc){
                std::cerr << "error: -o requires an argument\n";
                return 2;
            }
            outputPath = argv[++i];
        }
        else if (arg == "--dump-tokens"){
            dumpTokens = true;
        }
        /*else if (arg == "--dump-ast"){
            dumpAst = true;
        }*/
        else if (arg == "-h" || arg == "--help"){
            std::cout << "Usage: myc <source> [-o <output>] [--dump-tokens] [--dump-ast]\n";
            return 0;
        }
        else if (!arg.empty() && arg[0] == '-'){
            std::cerr << "error: unknown option '" << arg << "'\n";
            return 2;
        }
        else if (sourcePath.empty()){
            sourcePath = arg;
        }
        else{
            std::cerr << "error: unexpected argument '" << arg << "'\n";
            return 2;
        }
    }

    if (sourcePath.empty()){
        std::cerr << "Usage: myc <source> [-o <output>] [--dump-tokens] [--dump-ast]\n";
        return 2;
    }

    std::ifstream code_file(sourcePath);
    if (!code_file.is_open()){
        std::cerr << "error: cannot open '" << sourcePath << "'\n";
        return 1;
    }
    std::stringstream buf;
    buf << code_file.rdbuf();
    std::string source = buf.str();

    auto tokens = tokenize(source, sourcePath);
    if (!tokens) {
        std::cerr << tokens.error() << "\n";
        return 1;
    }

    if (dumpTokens){
        for (const auto& t : *tokens){
            std::cout << sourcePath << ":" << t.line << ":" << t.column
                      << "\ttype=" << static_cast<int>(t.type)
                      << "\tlexeme='" << t.lexeme << "'\n";
        }
    }

    auto nodes = parse(*tokens, sourcePath);
    if (!nodes) {
        std::cerr << nodes.error() << "\n";
        return 1;
    }

    Program program;
    program.decls = *nodes;

    if (dumpAst){
        for (Stmt* decl : program.decls){
            std::string kind = "Unknown";
            std::string name;

            if (auto* f = dynamic_cast<FuncDecl*>(decl)) {
                kind = "FuncDecl";      
                name = f->name; 
            }
            else if (auto* s = dynamic_cast<StructDecl*>(decl)) { 
                kind = "StructDecl";    
                name = s->name; 
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(decl)) { 
                kind = "ClassDecl";     
                name = c->name; 
            }
            else if (auto* v = dynamic_cast<VarDecl*>(decl)) { 
                kind = "VarDecl";       
                name = v->name; 
            }
            else if (auto* t = dynamic_cast<TypeAlias*>(decl)) {
                kind = "TypeAlias";     
                name = t->alias; 
            }
            else if (auto* n = dynamic_cast<NamespaceDecl*>(decl)) { 
                kind = "NamespaceDecl"; 
                name = n->name; 
            }
            else if (auto* im = dynamic_cast<ImportDecl*>(decl)) { 
                kind = "ImportDecl";    
                name = im->path; 
            }
            else if (dynamic_cast<ExportDecl*>(decl)) { 
                kind = "ExportDecl"; 
            }
            
            std::cout << sourcePath << ":" << decl->line << ":" << decl->column << "\t" << kind << "\t" << name << "\n";
        }
    }

    SemanticAnalyzer analyzer;
    auto result = analyzer.analyze(&program, sourcePath);
    if (!result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    if (outputPath.empty()){
        outputPath = std::filesystem::path(sourcePath).stem().string();
    }
    outputPath = (std::filesystem::path("executables") / std::filesystem::path(outputPath).filename()).string();

    CodeGen codegen;
    auto codegenResult = codegen.generate(&program, outputPath);
    if (!codegenResult) {
        std::cerr << codegenResult.error() << "\n";
        return 1;
    }

    return 0;
}
