module;

#include <expected>
#include <memory>
#include <string>

export module symbol_table;

import ast;

export class SymbolTable {
    std::shared_ptr<Scope> current;

public:
    SymbolTable() : current(std::make_shared<Scope>()) {}

    void enterScope() {
        auto inner = std::make_shared<Scope>();
        inner->parent = current;
        current = inner;
    }

    void pushScope(std::shared_ptr<Scope> scope) {
        scope->parent = current;
        current = scope;
    }

    void exitScope() {
        if (current->parent) {
            current = current->parent;
        }
    }

    std::expected<void, std::string> declare(std::shared_ptr<Symbol> sym) {
        current->symbols[sym->name] = sym;
        return {};
    }

    std::shared_ptr<Scope> currentScope() {
        return current;
    }

    std::shared_ptr<Symbol> resolve(const std::string& name) {
        for (auto scope = current; scope != nullptr; scope = scope->parent) {
            auto it = scope->symbols.find(name);
            if (it != scope->symbols.end()) {
                return it->second;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Symbol> resolveCurrentScope(const std::string& name) {
        auto it = current->symbols.find(name);
        if (it != current->symbols.end()) {
            return it->second;
        }
        return nullptr;
    }
};
