#pragma once

#include "ast.hpp"
#include "scope.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace sema {

	class RecursionDepthExceeded : public std::runtime_error {
	public:
		explicit RecursionDepthExceeded(std::size_t max_depth)
				: std::runtime_error(
				"Recursion depth exceeded: max = " + std::to_string(max_depth)),
				  max_depth_(max_depth) {}

		[[nodiscard]] std::size_t max_depth() const noexcept { return max_depth_; }

	private:
		std::size_t max_depth_;
	};

	class RecursionGuard {
	public:
		RecursionGuard(std::size_t& depth, std::size_t max_depth)
				: depth_(depth), max_depth_(max_depth) {
			++depth_;
			if (depth_ > max_depth_) {
				--depth_;
				throw RecursionDepthExceeded(max_depth_);
			}
		}
		~RecursionGuard() { --depth_; }

		RecursionGuard(const RecursionGuard&) = delete;
		RecursionGuard& operator=(const RecursionGuard&) = delete;

	private:
		std::size_t& depth_;
		std::size_t  max_depth_;
	};

	class SemanticAnalyzer {
	public:
		static constexpr std::size_t kMaxRecursionDepth = 256;

		void analyze(const ast::AstTree& tree);

		[[nodiscard]] const ScopeStack& scopes() const noexcept { return scopes_; }

	private:
		void visit(ast::ConstNodeIt node);

		Type check_expression(ast::ConstNodeIt node);
		Type check_type_node(ast::ConstNodeIt node);

		void visit_program(ast::ConstNodeIt node);
		void visit_function(ast::ConstNodeIt node);
		void visit_block(ast::ConstNodeIt node);
		void visit_for(ast::ConstNodeIt node);
		void visit_var_decl(ast::ConstNodeIt node);
		void visit_identifier(ast::ConstNodeIt node);
		void visit_call(ast::ConstNodeIt node);
		void visit_children(ast::ConstNodeIt node);
		void visit_return(ast::ConstNodeIt node);
		void visit_conditional(ast::ConstNodeIt node);

		Type current_function_return_type_ = Type::Unknown;

		ScopeStack  scopes_;
		std::size_t depth_ = 0;
	};

} // namespace sema