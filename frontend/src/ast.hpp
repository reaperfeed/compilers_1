#pragma once

#include "tree.hpp"
#include "source_location.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <variant>

namespace ast {
// тип узла
	enum class NodeKind {
		// корень и функции
		Program,
		Function,
		// типы
		TypePrim,        // value: TypeKind
		TypePtr,         // ребёнок: TypePtr -> TypePrim
		TypeArray,       // ребёнок: TypeArray -> TypePrim
		// инструкции
		Block,
		VarDecl,
		ExprStmt,
		If,
		Elif,            // отдельный узел для elif-ветки
		Else,
		While,
		For,
		Return,
		// выражения
		Number,          // value: int64_t
		BoolLit,         // value: bool
		CharLit,         // value: char
		StringLit,       // value: std::string
		Identifier,      // value: std::string
		Binary,          // value: BinaryOp
		Unary,           // value: UnaryOp
		Assign,          // value: AssignOp
		Call,
		Index,
	};

	enum class TypeKind { Int, Bool, Char };

	enum class BinaryOp {
		Add, Sub, Mul, Div, Mod,
		Eq, Ne, Lt, Gt, Le, Ge,
		BitAnd, BitOr, BitXor, Shl, Shr,
		LogAnd, LogOr,
	};

	enum class UnaryOp { Neg, Not, BitNot, Deref, Addr };

	enum class AssignOp { Assign, Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr };

// полезная нагрузка узла
	struct AstNode {
		NodeKind kind = NodeKind::Program;
		SourceLocation loc;     //для индексации при ошибке
		std::variant<
				std::monostate,
				int64_t,
				bool,
				char,
				std::string,
				TypeKind,
				BinaryOp,
				UnaryOp,
				AssignOp
		> value;
	};

// роль ребёнка
	enum class EdgeRole {
		// Program -> Function
		Func,

		// Function
		ReturnType,
		Param,          // Function -> VarDecl (параметр как объявление)
		Body,           // Function -> Block

		// VarDecl
		DeclType,
		Init,           // может отсутствовать
		ArrSize,        // VarDecl -> Expr (для T name[N])

		// Block
		Stmt,

		// If / Elif / Else
		Cond,
		Then,
		ElifBranch,     // If -> Elif / Else
		ElseBranch,

		// While / For
		LoopBody,
		ForInit,
		ForCond,
		ForStep,

		// Return
		RetValue,

		// ExprStmt
		Expr,

		// выражения
		Lhs,
		Rhs,
		Operand,
		Callee,
		Arg,
		Base,
		Subscript,
	};

	using AstTree = tree::tree_t<AstNode, EdgeRole>;
	using NodeIt  = AstTree::node_iterator;
	using EdgeIt  = AstTree::edge_iterator;
	using ConstNodeIt = AstTree::const_node_iterator;
	using ConstEdgeIt = AstTree::const_edge_iterator;

// фабрика
	class Builder {
	public:
		NodeIt make(NodeKind kind, SourceLocation loc) {
			return tree_.new_node(AstNode{kind, loc, std::monostate{}});
		}
		NodeIt number(int64_t v, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::Number, loc, v});
		}
		NodeIt boolean(bool v, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::BoolLit, loc, v});
		}
		NodeIt ch(char c, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::CharLit, loc, c});
		}
		NodeIt string(std::string s, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::StringLit, loc, std::move(s)});
		}
		NodeIt identifier(std::string s, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::Identifier, loc, std::move(s)});
		}
		NodeIt type_prim(TypeKind k, SourceLocation loc) {
			return tree_.new_node(AstNode{NodeKind::TypePrim, loc, k});
		}
		NodeIt binary(BinaryOp op, NodeIt lhs, NodeIt rhs, SourceLocation loc) {
			auto n = tree_.new_node(AstNode{NodeKind::Binary, loc, op});
			link(n, EdgeRole::Lhs, lhs);
			link(n, EdgeRole::Rhs, rhs);
			return n;
		}
		NodeIt unary(UnaryOp op, NodeIt operand, SourceLocation loc) {
			auto n = tree_.new_node(AstNode{NodeKind::Unary, loc, op});
			link(n, EdgeRole::Operand, operand);
			return n;
		}
		NodeIt assign(AssignOp op, NodeIt lhs, NodeIt rhs, SourceLocation loc) {
			auto n = tree_.new_node(AstNode{NodeKind::Assign, loc, op});
			link(n, EdgeRole::Lhs, lhs);
			link(n, EdgeRole::Rhs, rhs);
			return n;
		}

		// связывание
		EdgeIt link(NodeIt parent, EdgeRole role, NodeIt child) {
			return tree_.new_edge(parent, child, role);
		}

		// доступ к данным узла
		AstNode& node(NodeIt it) noexcept { return it->data(); }
		[[nodiscard]] const AstNode& node(NodeIt it) const noexcept { return it->data(); }

		AstTree finish() && { return std::move(tree_); }
		AstTree &tree() noexcept { return tree_; }
		[[nodiscard]] const AstTree &tree() const noexcept { return tree_; }

	private:
		AstTree tree_;
	};

// --- Диагностика: печать AST в dot ---
	void dump_dot(const AstTree &t, std::ostream &out);

} // namespace ast