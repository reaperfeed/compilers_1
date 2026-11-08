#include "ast.hpp"
#include "errors.hpp"
#include "interpreter.hpp"
#include "lexer.hpp"
#include "hir/lowering.hpp"
#include "hir/optimizer.hpp"
#include "lir/lowering.hpp"
#include "lir/c_codegen.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void usage(const char* executable) {
    std::cerr << "Usage: " << executable << " [--optimize] [--check-only] [--hir PATH] [--cfg PATH] [--cfg-before PATH] [--lir PATH] [--emit-c PATH] [--ast PATH] <file.lang> [args...]\n";
}
template <class Action>
void write(const std::string& path, Action&& action) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot open output file: " + path);
    action(out);
    out.close();
    if (!out) throw std::runtime_error("cannot write output file: " + path);
}

void write_outputs(const ast::AstTree& tree, const hir::TranslationUnit& unit,
                   const lir::TranslationUnit& low_unit, const std::string& ast_path,
                   const std::string& hir_path, const std::string& cfg_path, const std::string& lir_path) {
    write(ast_path, [&](auto& out) { ast::dump_dot(tree, out); });
    write(hir_path, [&](auto& out) { hir::dump(unit, out); });
    write(cfg_path, [&](auto& out) { hir::dump_cfg_dot(unit, out); });
    write(lir_path, [&](auto& out) { lir::dump(low_unit, out); });
    std::cout << "[AST written to " << ast_path << "]\n[HIR verified and written to " << hir_path
              << "]\n[CFG written to " << cfg_path << "]\n[LIR written to " << lir_path << "]\n";
}
}
int main(int argc, char** argv) {
    bool check_only = false;
    bool optimize = false;
    std::string ast_path = "ast.dot", hir_path = "hir.txt", cfg_path = "cfg.dot", lir_path = "lir.txt";
    std::string c_path, cfg_before_path;
    int index = 1;
    while (index < argc) {
        std::string option = argv[index];
        if (option == "--") { ++index; break; }
        if (option == "--help" || option == "-h") { usage(argv[0]); return 0; }
        if (option == "--optimize") { optimize = true; ++index; continue; }
        if (option == "--check-only") { check_only = true; ++index; continue; }
        if (option == "--hir" || option == "--cfg" || option == "--cfg-before" || option == "--lir" || option == "--emit-c" || option == "--ast") {
            if (index + 1 >= argc) { usage(argv[0]); return 1; }
            if (option == "--hir") hir_path = argv[index + 1];
            else if (option == "--cfg") cfg_path = argv[index + 1];
            else if (option == "--cfg-before") cfg_before_path = argv[index + 1];
            else if (option == "--lir") lir_path = argv[index + 1];
            else if (option == "--emit-c") c_path = argv[index + 1];
            else ast_path = argv[index + 1];
            index += 2; continue;
        }
        break;
    }
    if (index >= argc) { usage(argv[0]); return 1; }
    const char* path = argv[index];
    std::vector<std::string> script_args;
    for (int i = index; i < argc; ++i) script_args.emplace_back(argv[i]);
    FILE* input = std::fopen(path, "r");
    if (!input) { std::cerr << "Cannot open file: " << path << '\n'; return 1; }
    ast::Builder builder;
    try {
        reset_lexer(input);
        yy::Parser parser(builder);
        int result = parser.parse();
        std::fclose(input); input = nullptr;
        if (result) return 1;
        sema::SemanticAnalyzer analyzer;
        analyzer.analyze(builder.tree());
        auto unit = hir::lower(builder.tree());
        if (!cfg_before_path.empty()) {
            write(cfg_before_path, [&](auto& out) { hir::dump_cfg_dot(unit, out); });
            std::cout << "[CFG before optimization written to " << cfg_before_path << "]\n";
        }
        if (optimize) hir::optimize_local(unit);
        auto low_unit = lir::lower(unit);
        write_outputs(builder.tree(), unit, low_unit, ast_path, hir_path, cfg_path, lir_path);
        if (!c_path.empty()) {
            std::ofstream out(c_path);
            if (!out) throw std::runtime_error("cannot open output file: " + c_path);
            lir::emit_c(low_unit, out);
            out.close();
            if (!out) throw std::runtime_error("cannot write output file: " + c_path);
            std::cout << "[C written to " << c_path << "]\n";
        }
        if (check_only || !c_path.empty()) return 0;
        interp::Interpreter interpreter(std::cout);
        interp::Value result_value = interpreter.run(builder.tree(), script_args);
        if (auto* v = std::get_if<int64_t>(&result_value.data)) std::cout << "\nProgram returned " << *v << '\n';
        else if (auto* v = std::get_if<double>(&result_value.data)) std::cout << "\nProgram returned " << *v << '\n';
        else if (auto* v = std::get_if<bool>(&result_value.data)) std::cout << "\nProgram returned " << (*v ? "true" : "false") << '\n';
        else if (auto* v = std::get_if<char>(&result_value.data)) std::cout << "\nProgram returned '" << *v << "'\n";
        else std::cout << "\nProgram finished.\n";
    } catch (const err::CompileError& e) {
        if (input) std::fclose(input);
        std::cerr << path << ':' << e.location().line << ':' << e.location().column << ": error: ["
                  << err::stage_name(e.stage()) << "] " << e.what() << '\n';
        return 1;
    } catch (const hir::ValidationError& e) {
        if (input) std::fclose(input);
        std::cerr << path << ": error: [HIR] " << e.what() << '\n'; return 1;
    } catch (const interp::RuntimeError& e) {
        std::cerr << "\nRuntime error: " << e.what() << '\n'; return 2;
    } catch (const std::exception& e) {
        if (input) std::fclose(input);
        std::cerr << path << ": error: " << e.what() << '\n'; return 3;
    }
    return 0;
}
