#include "ast.hpp"

#include <ostream>
#include <string>

namespace ast {
	namespace {
		std::string kind_name(NodeKind k) {
			switch (k) {
				case NodeKind::Program:    return "Program";
				case NodeKind::Function:   return "Function";
				case NodeKind::TypePrim:   return "Type";
				case NodeKind::TypePtr:    return "Ptr";
				case NodeKind::TypeArray:  return "Array";
				case NodeKind::Block:      return "Block";
				case NodeKind::VarDecl:    return "VarDecl";
				case NodeKind::ExprStmt:   return "ExprStmt";
				case NodeKind::If:         return "If";
				case NodeKind::Elif:       return "Elif";
				case NodeKind::Else:       return "Else";
				case NodeKind::While:      return "While";
				case NodeKind::For:        return "For";
				case NodeKind::Return:     return "Return";
				case NodeKind::Number:     return "Number";
				case NodeKind::FloatLit:   return "Float";
				case NodeKind::BoolLit:    return "Bool";
				case NodeKind::CharLit:    return "Char";
				case NodeKind::StringLit:  return "String";
				case NodeKind::Identifier: return "Ident";
				case NodeKind::Binary:     return "Binary";
				case NodeKind::Unary:      return "Unary";
				case NodeKind::Assign:     return "Assign";
				case NodeKind::Call:       return "Call";
				case NodeKind::Index:      return "Index";
			}
			return "?";
		}

		std::string binary_op_name(BinaryOp op) {
			switch (op) {
				case BinaryOp::Add:    return "+";
				case BinaryOp::Sub:    return "-";
				case BinaryOp::Mul:    return "*";
				case BinaryOp::Div:    return "/";
				case BinaryOp::Mod:    return "%";
				case BinaryOp::Eq:     return "==";
				case BinaryOp::Ne:     return "!=";
				case BinaryOp::Lt:     return "<";
				case BinaryOp::Gt:     return ">";
				case BinaryOp::Le:     return "<=";
				case BinaryOp::Ge:     return ">=";
				case BinaryOp::BitAnd: return "&";
				case BinaryOp::BitOr:  return "|";
				case BinaryOp::BitXor: return "^";
				case BinaryOp::Shl:    return "<<";
				case BinaryOp::Shr:    return ">>";
				case BinaryOp::LogAnd: return "&&";
				case BinaryOp::LogOr:  return "||";
			}
			return "?";
		}

		std::string unary_op_name(UnaryOp op) {
			switch (op) {
				case UnaryOp::Neg:    return "-";
				case UnaryOp::Not:    return "!";
				case UnaryOp::BitNot: return "~";
				case UnaryOp::Deref:  return "*";
				case UnaryOp::Addr:   return "&";
			}
			return "?";
		}

		std::string assign_op_name(AssignOp op) {
			switch (op) {
				case AssignOp::Assign: return "=";
				case AssignOp::Add:    return "+=";
				case AssignOp::Sub:    return "-=";
				case AssignOp::Mul:    return "*=";
				case AssignOp::Div:    return "/=";
				case AssignOp::Mod:    return "%=";
				case AssignOp::BitAnd: return "&=";
				case AssignOp::BitOr:  return "|=";
				case AssignOp::BitXor: return "^=";
				case AssignOp::Shl:    return "<<=";
				case AssignOp::Shr:    return ">>=";
			}
			return "?";
		}

		std::string node_label(const AstNode &n) {
			std::string base = kind_name(n.kind);
			std::visit([&](auto &&v) {
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::monostate>) {
				} else if constexpr (std::is_same_v<T, int64_t>) {
					base += ": " + std::to_string(v);
				} else if constexpr (std::is_same_v<T, double>) {
					base += ": " + std::to_string(v);
				} else if constexpr (std::is_same_v<T, bool>) {
					base += v ? ": true" : ": false";
				} else if constexpr (std::is_same_v<T, char>) {
					base += std::string(": '") + v + "'";
				} else if constexpr (std::is_same_v<T, std::string>) {
					base += ": " + v;
				} else if constexpr (std::is_same_v<T, TypeKind>) {
					base += v == TypeKind::Int  ? ": i32"
					                            : v == TypeKind::Float ? ": f32"
												: v == TypeKind::Void  ? ": void"
					                            : v == TypeKind::Bool ? ": bool"
					                                                  : ": char";
				} else if constexpr (std::is_same_v<T, BinaryOp>) {
					base += ": " + binary_op_name(v);
				} else if constexpr (std::is_same_v<T, UnaryOp>) {
					base += ": " + unary_op_name(v);
				} else if constexpr (std::is_same_v<T, AssignOp>) {
					base += ": " + assign_op_name(v);
				}
			}, n.value);
			return base;
		}

		std::string role_name(EdgeRole r) {
			switch (r) {
				case EdgeRole::Func:        return "func";
				case EdgeRole::ReturnType:  return "ret";
				case EdgeRole::Param:       return "param";
				case EdgeRole::Body:        return "body";
				case EdgeRole::DeclType:    return "type";
				case EdgeRole::Init:        return "init";
				case EdgeRole::ArrSize:     return "size";
				case EdgeRole::Stmt:        return "stmt";
				case EdgeRole::Cond:        return "cond";
				case EdgeRole::Then:        return "then";
				case EdgeRole::ElifBranch:  return "elif";
				case EdgeRole::ElseBranch:  return "else";
				case EdgeRole::LoopBody:    return "do";
				case EdgeRole::ForInit:     return "init";
				case EdgeRole::ForCond:     return "cond";
				case EdgeRole::ForStep:     return "step";
				case EdgeRole::RetValue:    return "value";
				case EdgeRole::Expr:        return "expr";
				case EdgeRole::Lhs:         return "lhs";
				case EdgeRole::Rhs:         return "rhs";
				case EdgeRole::Operand:     return "op";
				case EdgeRole::Callee:      return "callee";
				case EdgeRole::Arg:         return "arg";
				case EdgeRole::Base:        return "base";
				case EdgeRole::Subscript:   return "index";
			}
			return "?";
		}

	} // namespace

	void dump_dot(const AstTree &t, std::ostream &out) {
		t.write_dot(
				out,
				[](const AstNode &n) { return node_label(n); },
				[](EdgeRole r)       { return role_name(r); }
		);
	}

} // namespace ast