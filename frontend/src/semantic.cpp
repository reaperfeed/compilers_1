#include "semantic.hpp"

#include "errors.hpp"

#include <variant>

namespace sema {

	namespace {

		const std::string* node_name(const ast::AstNode& n) {
			return std::get_if<std::string>(&n.value);
		}

		template <typename Fn>
		void for_each_child_with_role(ast::ConstNodeIt node,
		                              ast::EdgeRole role, Fn&& fn) {
			for (auto it = node->out_begin(); it != node->out_end(); ++it) {
				auto edge = *it;
				if (edge->data() == role) fn(edge->next_node());
			}
		}

// встроенные функции, доступные всегда
		constexpr const char* kBuiltins[] = {"prints", "printi", "printf"};

	} // namespace

	void SemanticAnalyzer::analyze(const ast::AstTree& tree) {
		// глобальный scope уже создан ScopeStack
		// регистрируем встроенные функции, чтобы они не считались "undeclared"
		for (auto name : kBuiltins) {
			scopes_.current().declare(Symbol{name, Symbol::Kind::Function, {}});
		}

		if (tree.begin() == tree.end()) return;
		auto root = tree.root();
		if (root == tree.end()) return;
		depth_ = 0;
		visit(root);
	}

	void SemanticAnalyzer::visit(ast::ConstNodeIt node) {
		RecursionGuard guard(depth_, kMaxRecursionDepth);

		switch (node->data().kind) {
			case ast::NodeKind::Program:    visit_program(node);    break;
			case ast::NodeKind::Function:   visit_function(node);   break;
			case ast::NodeKind::Block:      visit_block(node);      break;
			case ast::NodeKind::For:        visit_for(node);        break;
			case ast::NodeKind::VarDecl:    visit_var_decl(node);   break;
			case ast::NodeKind::Identifier: visit_identifier(node); break;
			case ast::NodeKind::Call:       visit_call(node);       break;
			default:                        visit_children(node);   break;
		}
	}

	void SemanticAnalyzer::visit_children(ast::ConstNodeIt node) {
		for (auto it = node->out_begin(); it != node->out_end(); ++it) {
			visit((*it)->next_node());
		}
	}

	void SemanticAnalyzer::visit_program(ast::ConstNodeIt node) {
		// регистрируем имена всех функций в глобальном scope.
		for_each_child_with_role(node, ast::EdgeRole::Func,
		                         [this](ast::ConstNodeIt fn) {
			                         if (auto* name = node_name(fn->data())) {
				                         if (!scopes_.current().declare(
						                         Symbol{*name, Symbol::Kind::Function, fn->data().loc})) {
					                         throw err::CompileError(
							                         err::Stage::Semantic, fn->data().loc,
							                         "redefinition of function '" + *name + "'");
				                         }
			                         }
		                         });

		// обходим тела функций
		for_each_child_with_role(node, ast::EdgeRole::Func,
		                         [this](ast::ConstNodeIt fn) { visit(fn); });
	}

	void SemanticAnalyzer::visit_function(ast::ConstNodeIt node) {
		// новый scope для тела функции
		ScopeGuard sg(scopes_);

		// параметры объявляются в этом scope
		for_each_child_with_role(node, ast::EdgeRole::Param,
		                         [this](ast::ConstNodeIt param) {
			                         if (auto* name = node_name(param->data())) {
				                         if (!scopes_.current().declare(
						                         Symbol{*name, Symbol::Kind::Parameter, param->data().loc})) {
					                         throw err::CompileError(
							                         err::Stage::Semantic, param->data().loc,
							                         "duplicate parameter '" + *name + "'");
				                         }
			                         }
			                         for_each_child_with_role(param, ast::EdgeRole::DeclType,
			                                                  [this](ast::ConstNodeIt t) { visit(t); });
		                         });

		// тело функции — снова открывает свой scope внутри visit_block
		for_each_child_with_role(node, ast::EdgeRole::Body,
		                         [this](ast::ConstNodeIt body) { visit(body); });
	}

	void SemanticAnalyzer::visit_block(ast::ConstNodeIt node) {
		ScopeGuard sg(scopes_);
		visit_children(node);
	}

	void SemanticAnalyzer::visit_for(ast::ConstNodeIt node) {
		ScopeGuard sg(scopes_);
		visit_children(node);
	}

	void SemanticAnalyzer::visit_var_decl(ast::ConstNodeIt node) {
		if (auto* name = node_name(node->data())) {
			if (!scopes_.current().declare(
					Symbol{*name, Symbol::Kind::Variable, node->data().loc})) {
				throw err::CompileError(
						err::Stage::Semantic, node->data().loc,
						"redefinition of '" + *name + "' in the same scope");
			}
		}
		visit_children(node);
	}

	void SemanticAnalyzer::visit_identifier(ast::ConstNodeIt node) {
		if (auto* name = node_name(node->data())) {
			if (scopes_.current().lookup(*name) == nullptr) {
				throw err::CompileError(
						err::Stage::Semantic, node->data().loc,
						"use of undeclared identifier '" + *name + "'");
			}
		}
	}

	void SemanticAnalyzer::visit_call(ast::ConstNodeIt node) {
		// проверяем сам callee как identifier, потом аргументы
		for_each_child_with_role(node, ast::EdgeRole::Callee,
		                         [this](ast::ConstNodeIt callee) {
			                         if (callee->data().kind == ast::NodeKind::Identifier) {
				                         if (auto* name = node_name(callee->data())) {
					                         if (scopes_.current().lookup(*name) == nullptr) {
						                         throw err::CompileError(
								                         err::Stage::Semantic, callee->data().loc,
								                         "call to undeclared function '" + *name + "'");
					                         }
				                         }
			                         } else {
				                         visit(callee);
			                         }
		                         });

		for_each_child_with_role(node, ast::EdgeRole::Arg,
		                         [this](ast::ConstNodeIt arg) { visit(arg); });
	}

} // namespace sema