#pragma once

#include "core/string.h"
#include "token.h"

namespace Lumix::LumScript {

struct Module;
using ImportResolver = bool (*)(Module& module, StringView path, StringView alias, StringView* source, void* userdata);

struct Callbacks {
    virtual ~Callbacks() {}
    virtual void print(StringView msg) = 0;

    bool has_error = false;

    void print(int v);

    template <typename... Args> void errorAt(const Token& token, Args&&... args) {
        if (has_error) return;
        
        if (!token.source_name.empty()) {
            print(token.source_name);
            print(": ");
        }
        print("line ");
        print(token.line);
        print(", column ");
        print(token.column);
        print(": ");
        int dummy[] = {
            (print(static_cast<Args&&>(args)), 0)...,
        };
        print("\n");
    }
    
    template <typename... Args> void error(Args&&... args) { 
        if (has_error) return;
        int dummy[] = {
            (print(static_cast<Args&&>(args)), 0)...,
        };
    }
};

bool typecheck(Module& module, Callbacks& diagnostics);
bool parse(Module& module, StringView source, Callbacks& diagnostics, StringView declaration_prefix = {}, StringView source_name = {});
bool compile(Module& module, StringView source, Callbacks& diagnostics, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr, StringView source_name = {});

} // namespace Lumix::LumScript
