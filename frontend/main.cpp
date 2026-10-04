#include "ast.hpp"
#include "errors.hpp"
#include "interpreter.hpp"
#include "parser.hpp"
#include "semantic.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern FILE* yyin;

static int report_error(const err::CompileError& e, const char* file) {
	std::cerr << file << ":"
	          << e.location().line << ":"
	          << e.location().column << ": error: ["
	          << err::stage_name(e.stage()) << "] "
	          << e.what() << "\n";
	return 1;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <file.lang> [args...]\n";
		return 1;
	}

	const char* path = argv[1];
	yyin = std::fopen(path, "r");
	if (!yyin) {
		std::cerr << "Cannot open file: " << path << "\n";
		return 1;
	}

	ast::Builder builder;

	try {
		yy::Parser parser(builder);
		int parse_rc = parser.parse();
		std::fclose(yyin);
		yyin = nullptr;
		if (parse_rc != 0) return 1;

		sema::SemanticAnalyzer analyzer;
		analyzer.analyze(builder.tree());
	} catch (const err::CompileError& e) {
		if (yyin) std::fclose(yyin);
		return report_error(e, path);
	} catch (const std::exception& e) {
		if (yyin) std::fclose(yyin);
		std::cerr << path << ": internal error: " << e.what() << "\n";
		return 3;
	}

	// AST в ast.dot.
	{
		std::ofstream dot("ast.dot");
		if (dot) {
			ast::dump_dot(builder.tree(), dot);
			std::cout << "[AST written to ast.dot]\n";
		} else {
			std::cerr << "warning: cannot write ast.dot\n";
		}
	}

	// Аргументы скрипта: argv[0] = путь скрипта, дальше — остальные.
	std::vector<std::string> script_args;
	for (int i = 1; i < argc; ++i) {
		script_args.emplace_back(argv[i]);
	}

	try {
		interp::Interpreter interpreter(std::cout);
		interp::Value result = interpreter.run(builder.tree(), script_args);

		if (auto* v = std::get_if<int64_t>(&result.data)) {
			std::cout << "\nProgram returned " << *v << "\n";
		} else if (auto* d = std::get_if<double>(&result.data)) {
			std::cout << "\nProgram returned " << *d << "\n";
		} else if (auto* b = std::get_if<bool>(&result.data)) {
			std::cout << "\nProgram returned " << (*b ? "true" : "false") << "\n";
		} else if (auto* c = std::get_if<char>(&result.data)) {
			std::cout << "\nProgram returned '" << *c << "'\n";
		} else {
			std::cout << "\nProgram finished.\n";
		}
	} catch (const interp::RuntimeError& e) {
		std::cerr << "\nRuntime error: " << e.what() << "\n";
		return 2;
	}

	return 0;
}