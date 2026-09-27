#pragma once

#include "ast.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace interp {

	struct Value;
	using ValuePtr = std::shared_ptr<Value>;
	using Array    = std::shared_ptr<std::vector<ValuePtr>>;

// Array и ValuePtr хранятся через shared_ptr, чтобы:
//   массивы разделялись между копиями Value (и не копировались при передаче);
//   указатели реально ссылались на один и тот же слот памяти
	struct Value {
		std::variant<
				std::monostate,   // void
				int64_t,
				bool,
				char,
				std::string,
				Array,
				ValuePtr          // указатель (может быть nullptr)
		> data;

		Value() : data(std::monostate{}) {}
		Value(int64_t v) : data(v) {}
		Value(bool v)    : data(v) {}
		Value(char v)    : data(v) {}
		Value(std::string v) : data(std::move(v)) {}
		Value(Array v)       : data(std::move(v)) {}
		Value(ValuePtr v)    : data(std::move(v)) {}
	};

	class RuntimeError : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

// используется для прерывания блока при return
	struct ReturnSignal { Value value; };

	class Interpreter {
	public:
		static constexpr std::size_t kMaxCallDepth      = 256;
		static constexpr std::size_t kMaxLoopIterations = 1'000'000;

		explicit Interpreter(std::ostream& out);

		// запускает main; script_args — то, что программа увидит как argv
		// (argv[0] — имя файла скрипта, дальше — остальные аргументы).
		Value run(const ast::AstTree& tree,
		          const std::vector<std::string>& script_args);

	private:
		using Scope = std::unordered_map<std::string, ValuePtr>;

		std::vector<Scope>                                scopes_;
		std::unordered_map<std::string, ast::ConstNodeIt> functions_;
		std::size_t                                       call_depth_ = 0;
		std::ostream&                                     out_;

		void     push_scope();
		void     pop_scope();
		void     declare(const std::string& name, ValuePtr v);
		ValuePtr lookup_ptr(const std::string& name);

		void  collect_functions(const ast::AstTree& tree);
		Value call_function(const std::string& name, std::vector<Value> args);

		// создаёт слот со значением по умолчанию для данного типа:
		//  TypePrim  -> 0 / false / '\0'
		//  TypePtr   -> nullptr
		//  TypeArray -> массив из N элементов по умолчанию (N = 0 если безразмерный)
		ValuePtr make_default_for_type(ast::ConstNodeIt type_node);

		Value    eval(ast::ConstNodeIt node);
		ValuePtr eval_lvalue(ast::ConstNodeIt node);   // для присваиваний и &
		void     exec(ast::ConstNodeIt node);

		static int64_t as_int (const Value& v);
		static bool    as_bool(const Value& v);

		Value eval_binary(ast::BinaryOp op, const Value& a, const Value& b);
		Value eval_unary (ast::UnaryOp  op, const Value& a);
	};

} // namespace interp