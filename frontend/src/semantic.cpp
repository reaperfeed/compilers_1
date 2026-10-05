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
	} // namespace

	void SemanticAnalyzer::analyze(const ast::AstTree& tree) {
		// prints, printi — с одним аргументом любого типа
		for (auto name : {"prints", "printi"}) {
			Symbol s{name, Symbol::Kind::Function, Type::Int, {}, false, {}};
			s.param_types = {Type::Unknown};   // принимаем любой тип
			scopes_.current().declare(std::move(s));
		}
		// printf — вариадический: первый аргумент string, дальше любые
		{
			Symbol s{"printf", Symbol::Kind::Function, Type::Int, {}, true, {}};
			s.param_types = {Type::String};
			scopes_.current().declare(std::move(s));
		}

		if (tree.begin() == tree.end()) return;
		auto root = tree.root();
		if (root == tree.end()) return;
		depth_ = 0;
		visit(root);
	}

	Type SemanticAnalyzer::check_type_node(ast::ConstNodeIt type_node) {
		if (type_node->data().kind == ast::NodeKind::TypePrim) {
			if (auto* tk = std::get_if<ast::TypeKind>(&type_node->data().value)) {
				switch (*tk) {
					case ast::TypeKind::Int:   return Type::Int;
					case ast::TypeKind::Float: return Type::Float;
					case ast::TypeKind::Void:  return Type::Void;
					case ast::TypeKind::Bool:  return Type::Bool;
					case ast::TypeKind::Char:  return Type::Char;
				}
			}
		}
		// Для массивов и указателей возвращаем Unknown
		return Type::Unknown;
	}

	Type SemanticAnalyzer::check_expression(ast::ConstNodeIt node) {
		RecursionGuard guard(depth_, kMaxRecursionDepth);

		switch (node->data().kind) {
			case ast::NodeKind::Number:    return Type::Int;
			case ast::NodeKind::FloatLit:  return Type::Float;
			case ast::NodeKind::BoolLit:   return Type::Bool;
			case ast::NodeKind::CharLit:   return Type::Char;
			case ast::NodeKind::StringLit: return Type::String;

			case ast::NodeKind::Identifier: {
				if (auto* name = node_name(node->data())) {
					const Symbol* sym = scopes_.current().lookup(*name);
					if (!sym) {
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "use of undeclared identifier '" + *name + "'");
					}
					return sym->type;
				}
				return Type::Unknown;
			}

			case ast::NodeKind::Binary: {
				Type lt = Type::Unknown, rt = Type::Unknown;
				auto op = std::get<ast::BinaryOp>(node->data().value);
				int idx = 0;
				for (auto it = node->out_begin(); it != node->out_end(); ++it, ++idx) {
					if (idx == 0) lt = check_expression((*it)->next_node());
					else if (idx == 1) rt = check_expression((*it)->next_node());
				}

				auto is_numeric = [](Type t) { return t == Type::Int || t == Type::Float; };
				auto promote = [&](Type a, Type b) -> Type {
					return (a == Type::Float || b == Type::Float) ? Type::Float : Type::Int;
				};

				switch (op) {
					case ast::BinaryOp::Add:
					case ast::BinaryOp::Sub:
					case ast::BinaryOp::Mul:
					case ast::BinaryOp::Div:
						if (!is_numeric(lt) || !is_numeric(rt)) {
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "invalid operands to arithmetic operator: '" +
							                        type_to_string(lt) + "' and '" + type_to_string(rt) + "'");
						}
						return promote(lt, rt);

					case ast::BinaryOp::Mod:
					case ast::BinaryOp::BitAnd: case ast::BinaryOp::BitOr:
					case ast::BinaryOp::BitXor: case ast::BinaryOp::Shl:
					case ast::BinaryOp::Shr:
						if (lt != Type::Int || rt != Type::Int) {
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "bitwise/modulo operator requires 'i32' operands, got '" +
							                        type_to_string(lt) + "' and '" + type_to_string(rt) + "'");
						}
						return Type::Int;

					case ast::BinaryOp::Eq: case ast::BinaryOp::Ne:
					case ast::BinaryOp::Lt: case ast::BinaryOp::Gt:
					case ast::BinaryOp::Le: case ast::BinaryOp::Ge:
						if (is_numeric(lt) && is_numeric(rt)) return Type::Bool;
						if (lt == rt) return Type::Bool;
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "cannot compare '" + type_to_string(lt) + "' and '" +
						                        type_to_string(rt) + "'");

					case ast::BinaryOp::LogAnd:
					case ast::BinaryOp::LogOr:
						if (lt != Type::Bool || rt != Type::Bool) {
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "logical operator requires 'bool' operands");
						}
						return Type::Bool;
				}
				return Type::Unknown;
			}

			case ast::NodeKind::Unary: {
				auto op = std::get<ast::UnaryOp>(node->data().value);
				Type ot = Type::Unknown;
				for (auto it = node->out_begin(); it != node->out_end(); ++it)
					ot = check_expression((*it)->next_node());
				switch (op) {
					case ast::UnaryOp::Neg:
						if (ot != Type::Int && ot != Type::Float)
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "unary '-' requires numeric operand, got '" +
							                        type_to_string(ot) + "'");
						return ot;
					case ast::UnaryOp::Not:
						if (ot != Type::Bool)
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "'!' requires 'bool' operand");
						return Type::Bool;
					case ast::UnaryOp::BitNot:
						if (ot != Type::Int)
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "'~' requires 'i32' operand");
						return Type::Int;
					default:
						return ot;   // Deref / Addr — упрощение
				}
			}

			case ast::NodeKind::Assign: {
				auto op = std::get<ast::AssignOp>(node->data().value);
				Type lt = Type::Unknown, rt = Type::Unknown;
				int idx = 0;
				for (auto it = node->out_begin(); it != node->out_end(); ++it, ++idx) {
					if (idx == 0) lt = check_expression((*it)->next_node());
					else if (idx == 1) rt = check_expression((*it)->next_node());
				}
				if (lt == Type::Unknown || rt == Type::Unknown) return lt;

				// Для составных присваиваний (+=, &=, ...) нужны числовые типы
				if (op != ast::AssignOp::Assign) {
					auto is_numeric = [](Type t) { return t == Type::Int || t == Type::Float; };
					auto is_bitwise = [](ast::AssignOp o) {
						return o == ast::AssignOp::BitAnd || o == ast::AssignOp::BitOr ||
						       o == ast::AssignOp::BitXor || o == ast::AssignOp::Shl ||
						       o == ast::AssignOp::Shr || o == ast::AssignOp::Mod;
					};
					if (is_bitwise(op) && (lt != Type::Int || rt != Type::Int)) {
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "bitwise/modulo-assign requires 'i32' operands");
					}
					if (!is_bitwise(op) && (!is_numeric(lt) || !is_numeric(rt))) {
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "arithmetic-assign requires numeric operands");
					}
				} else {
					if (!is_assignable(lt, rt)) {
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "cannot assign '" + type_to_string(rt) + "' to '" +
						                        type_to_string(lt) + "'");
					}
				}
				return lt;
			}

			case ast::NodeKind::Call: {
				// Получаем функцию
				const Symbol* fn = nullptr;
				std::string fn_name;
				for (auto it = node->out_begin(); it != node->out_end(); ++it) {
					auto edge = *it;
					if (edge->data() == ast::EdgeRole::Callee) {
						auto callee = edge->next_node();
						if (callee->data().kind == ast::NodeKind::Identifier) {
							if (auto* n = node_name(callee->data())) {
								fn_name = *n;
								fn = scopes_.current().lookup(*n);
								if (!fn) {
									throw err::CompileError(err::Stage::Semantic, callee->data().loc,
									                        "call to undeclared function '" + *n + "'");
								}
							}
						}
					}
				}

				// Собираем типы аргументов
				std::vector<Type> arg_types;
				for_each_child_with_role(node, ast::EdgeRole::Arg,
				                         [this, &arg_types](ast::ConstNodeIt a) {
					                         arg_types.push_back(check_expression(a));
				                         });

				if (fn && fn->kind == Symbol::Kind::Function) {
					// printf — вариадический, только первый аргумент проверяем
					if (fn->is_variadic) {
						if (arg_types.empty() || arg_types[0] != Type::String) {
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "printf requires format string as first argument");
						}
						return fn->type;
					}
					// Обычные функции — проверяем арность и типы
					if (arg_types.size() != fn->param_types.size()) {
						throw err::CompileError(err::Stage::Semantic, node->data().loc,
						                        "function '" + fn_name + "' expects " +
						                        std::to_string(fn->param_types.size()) + " arguments, got " +
						                        std::to_string(arg_types.size()));
					}
					for (size_t i = 0; i < arg_types.size(); ++i) {
						if (fn->param_types[i] != Type::Unknown &&
						    !is_assignable(fn->param_types[i], arg_types[i])) {
							throw err::CompileError(err::Stage::Semantic, node->data().loc,
							                        "argument " + std::to_string(i + 1) + " of '" + fn_name +
							                        "': expected '" + type_to_string(fn->param_types[i]) +
							                        "', got '" + type_to_string(arg_types[i]) + "'");
						}
					}
					return fn->type;
				}
				return Type::Unknown;
			}

			default:
				visit_children(node);
				return Type::Unknown;
		}
	}

	void SemanticAnalyzer::visit(ast::ConstNodeIt node) {
		RecursionGuard guard(depth_, kMaxRecursionDepth);

		switch (node->data().kind) {
			case ast::NodeKind::Program:    visit_program(node);    break;
			case ast::NodeKind::Function:   visit_function(node);   break;
			case ast::NodeKind::Block:      visit_block(node);      break;
			case ast::NodeKind::Return:     visit_return(node);     break;
			case ast::NodeKind::For:        visit_for(node);        break;
			case ast::NodeKind::VarDecl:    visit_var_decl(node);   break;
			case ast::NodeKind::Identifier: visit_identifier(node); break;
			case ast::NodeKind::Call:       visit_call(node);       break;
			case ast::NodeKind::If:
			case ast::NodeKind::Elif:
			case ast::NodeKind::While:      visit_conditional(node);break;
			default:                        visit_children(node);   break;
		}
	}

	void SemanticAnalyzer::visit_children(ast::ConstNodeIt node) {
		for (auto it = node->out_begin(); it != node->out_end(); ++it) {
			visit((*it)->next_node());
		}
	}

	void SemanticAnalyzer::visit_program(ast::ConstNodeIt node) {
		for_each_child_with_role(node, ast::EdgeRole::Func,
		                         [this](ast::ConstNodeIt fn) {
			                         if (auto* name = node_name(fn->data())) {
				                         Symbol sym{
						                         *name,
						                         Symbol::Kind::Function,
						                         Type::Unknown,
						                         {},
						                         false,
						                         fn->data().loc
				                         };
				                         if (!scopes_.current().declare(std::move(sym))) {
					                         throw err::CompileError(
							                         err::Stage::Semantic, fn->data().loc,
							                         "redefinition of function '" + *name + "'");
				                         }
			                         }
		                         });

		for_each_child_with_role(node, ast::EdgeRole::Func,
		                         [this](ast::ConstNodeIt fn) { visit(fn); });
	}

	void SemanticAnalyzer::visit_function(ast::ConstNodeIt node) {
		ScopeGuard sg(scopes_);

		// Определяем тип возврата
		Type return_type = Type::Void;
		for_each_child_with_role(node, ast::EdgeRole::ReturnType,
		                         [this, &return_type](ast::ConstNodeIt rt) {
			                         return_type = check_type_node(rt);
		                         });

		current_function_return_type_ = return_type;

		// Регистрируем параметры
		std::vector<Type> param_types;
		for_each_child_with_role(node, ast::EdgeRole::Param,
		                         [this, &param_types](ast::ConstNodeIt param) {
			                         Type pt = Type::Unknown;
			                         for_each_child_with_role(param, ast::EdgeRole::DeclType,
			                                                  [this, &pt](ast::ConstNodeIt t) { pt = check_type_node(t); });

			                         if (auto* name = node_name(param->data())) {
				                         Symbol sym{*name, Symbol::Kind::Parameter, pt, {}, false, param->data().loc};
				                         if (!scopes_.current().declare(std::move(sym))) {
					                         throw err::CompileError(err::Stage::Semantic, param->data().loc,
					                                                 "duplicate parameter '" + *name + "'");
				                         }
				                         param_types.push_back(pt);
			                         }
		                         });

		// Обновляем сигнатуру функции в глобальном scope
		if (auto* fn_name = node_name(node->data())) {
			Scope* parent_scope = scopes_.current().parent();
			if (parent_scope) {
				Symbol* fn_sym = parent_scope->lookup_mutable(*fn_name);
				if (fn_sym) {
					fn_sym->type = return_type;
					fn_sym->param_types = std::move(param_types);
				}
			}
		}

		// Обходим тело
		for_each_child_with_role(node, ast::EdgeRole::Body,
		                         [this](ast::ConstNodeIt body) { visit(body); });

		current_function_return_type_ = Type::Unknown;
	}

	void SemanticAnalyzer::visit_block(ast::ConstNodeIt node) {
		ScopeGuard sg(scopes_);
		visit_children(node);
	}

	void SemanticAnalyzer::visit_var_decl(ast::ConstNodeIt node) {
		Type declared_type = Type::Unknown;
		for_each_child_with_role(node, ast::EdgeRole::DeclType,
		                         [this, &declared_type](ast::ConstNodeIt t) { declared_type = check_type_node(t); });

		if (auto* name = node_name(node->data())) {
			Symbol sym{*name, Symbol::Kind::Variable, declared_type, {}, false, node->data().loc};
			if (!scopes_.current().declare(std::move(sym))) {
				throw err::CompileError(err::Stage::Semantic, node->data().loc,
				                        "redefinition of '" + *name + "' in the same scope");
			}
		}

		// Проверка инициализатора
		for_each_child_with_role(node, ast::EdgeRole::Init,
		                         [this, declared_type, name=node_name(node->data())](ast::ConstNodeIt init_node) {
			                         if (declared_type == Type::Unknown) return;   // массив/указатель — не проверяем
			                         Type init_type = check_expression(init_node);
			                         if (init_type == Type::Unknown) return;
			                         if (!is_assignable(declared_type, init_type)) {
				                         throw err::CompileError(err::Stage::Semantic, init_node->data().loc,
				                                                 "type mismatch: cannot initialize variable '" + (name ? *name : "?") +
				                                                 "' of type '" + type_to_string(declared_type) +
				                                                 "' with value of type '" + type_to_string(init_type) + "'");
			                         }
		                         });
	}

	void SemanticAnalyzer::visit_identifier(ast::ConstNodeIt node) {
		check_expression(node);
	}

	void SemanticAnalyzer::visit_call(ast::ConstNodeIt node) {
		check_expression(node);
	}

	void SemanticAnalyzer::visit_return(ast::ConstNodeIt node) {
		Type ret_type = Type::Void;
		for_each_child_with_role(node, ast::EdgeRole::RetValue,
		                         [this, &ret_type](ast::ConstNodeIt rv) {
			                         ret_type = check_expression(rv);
		                         });
		if (current_function_return_type_ == Type::Void && ret_type != Type::Void) {
			throw err::CompileError(err::Stage::Semantic, node->data().loc,
			                        "void function cannot return a value");
		}
		if (current_function_return_type_ != Type::Void &&
		    ret_type != Type::Unknown &&
		    !is_assignable(current_function_return_type_, ret_type)) {
			throw err::CompileError(err::Stage::Semantic, node->data().loc,
			                        "return type mismatch: function returns '" +
			                        type_to_string(current_function_return_type_) +
			                        "', but got '" + type_to_string(ret_type) + "'");
		}
		visit_children(node);
	}

	void SemanticAnalyzer::visit_conditional(ast::ConstNodeIt node) {
		for_each_child_with_role(node, ast::EdgeRole::Cond, [this](ast::ConstNodeIt c) {
			Type t = check_expression(c);
			if (t != Type::Bool && t != Type::Unknown) {
				throw err::CompileError(err::Stage::Semantic, c->data().loc,
				                        "condition must be 'bool', got '" + type_to_string(t) + "'");
			}
		});
		visit_children(node);
	}

	void SemanticAnalyzer::visit_for(ast::ConstNodeIt node) {
		ScopeGuard sg(scopes_);
		for_each_child_with_role(node, ast::EdgeRole::ForCond, [this](ast::ConstNodeIt c) {
			Type t = check_expression(c);
			if (t != Type::Bool && t != Type::Unknown) {
				throw err::CompileError(err::Stage::Semantic, c->data().loc,
				                        "for-loop condition must be 'bool', got '" + type_to_string(t) + "'");
			}
		});
		visit_children(node);
	}

} // namespace sema