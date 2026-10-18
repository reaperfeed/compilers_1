#include "interpreter.hpp"

#include <iostream>
#include <cmath>

namespace interp {

	namespace {

// пройтись по всем рёбрам node с заданной ролью и вызвать fn для детей
		template <typename Fn>
		void for_each_child(ast::ConstNodeIt node, ast::EdgeRole role, Fn&& fn) {
			for (auto it = node->out_begin(); it != node->out_end(); ++it) {
				auto edge = *it;
				if (edge->data() == role) fn(edge->next_node());
			}
		}

// первый ребёнок с данной ролью (или nullopt)
		std::optional<ast::ConstNodeIt>
		child_or_none(ast::ConstNodeIt node, ast::EdgeRole role) {
			for (auto it = node->out_begin(); it != node->out_end(); ++it) {
				auto edge = *it;
				if (edge->data() == role) return edge->next_node();
			}
			return std::nullopt;
		}

// размер массива из TypeArray (0 если безразмерный)
		int64_t array_size(ast::ConstNodeIt type_array) {
			for (auto it = type_array->out_begin(); it != type_array->out_end(); ++it) {
				auto edge = *it;
				if (edge->data() == ast::EdgeRole::ArrSize) {
					return std::get<int64_t>(edge->next_node()->data().value);
				}
			}
			return 0;
		}

	} // namespace

	Interpreter::Interpreter(std::ostream& out) : out_(out) {}

	void Interpreter::push_scope() { scopes_.emplace_back(); }
	void Interpreter::pop_scope()  { scopes_.pop_back(); }

	void Interpreter::declare(const std::string& name, ValuePtr v) {
		scopes_.back()[name] = std::move(v);
	}

	ValuePtr Interpreter::lookup_ptr(const std::string& name) {
		for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
			auto found = it->find(name);
			if (found != it->end()) return found->second;
		}
		throw RuntimeError("undefined identifier '" + name + "'");
	}

	int64_t Interpreter::as_int(const Value& v) {
		if (auto* n = std::get_if<int64_t>(&v.data)) return *n;
		if (auto* d = std::get_if<double>(&v.data))    return static_cast<int64_t>(*d);
		if (auto* b = std::get_if<bool>(&v.data))        return *b ? 1 : 0;
		if (auto* c = std::get_if<char>(&v.data))        return static_cast<int64_t>(*c);
		if (std::holds_alternative<std::monostate>(v.data)) return 0;
		throw RuntimeError("expected integer value");
	}

	bool Interpreter::as_bool(const Value& v) {
		if (auto* b = std::get_if<bool>(&v.data))     return *b;
		if (auto* n = std::get_if<int64_t>(&v.data))  return *n != 0;
		if (auto* d = std::get_if<double>(&v.data))   return *d != 0.0;
		if (auto* c = std::get_if<char>(&v.data))     return *c != 0;
		if (std::holds_alternative<std::monostate>(v.data)) return false;
		return true;  // массивы, строки, указатели считаются истинными, если не null
	}

	double Interpreter::as_double(const Value& v) { 
		if (auto* d = std::get_if<double>(&v.data))    return *d;
		if (auto* n = std::get_if<int64_t>(&v.data))   return static_cast<double>(*n);
		if (auto* c = std::get_if<char>(&v.data))      return static_cast<double>(*c);
		if (auto* b = std::get_if<bool>(&v.data))      return *b ? 1.0 : 0.0;
		throw RuntimeError("expected numeric value");
	}

	bool Interpreter::is_float(const Value& v) { 
		return std::holds_alternative<double>(v.data);
	}

	void Interpreter::collect_functions(const ast::AstTree& tree) {
		auto root = tree.root();
		for_each_child(root, ast::EdgeRole::Func, [this](ast::ConstNodeIt fn) {
			const auto& name = std::get<std::string>(fn->data().value);
			functions_[name] = fn;
		});
	}

	ValuePtr Interpreter::make_default_for_type(ast::ConstNodeIt type_node) {
		switch (type_node->data().kind) {
			case ast::NodeKind::TypePrim: {
				auto tk = std::get<ast::TypeKind>(type_node->data().value);
				switch (tk) {
					case ast::TypeKind::Int:  return std::make_shared<Value>(int64_t{0});
					case ast::TypeKind::Float: return std::make_shared<Value>(0.0);
					case ast::TypeKind::Bool: return std::make_shared<Value>(false);
					case ast::TypeKind::Char: return std::make_shared<Value>('\0');
				}
				break;
			}
			case ast::NodeKind::TypePtr:
				return std::make_shared<Value>(ValuePtr{nullptr});

			case ast::NodeKind::TypeArray: {
				auto base = child_or_none(type_node, ast::EdgeRole::Base);
				if (!base) throw RuntimeError("array type has no base");
				int64_t n = array_size(type_node);
				Array arr = std::make_shared<std::vector<ValuePtr>>();
				if (n > 0) {
					arr->reserve(static_cast<std::size_t>(n));
					for (int64_t i = 0; i < n; ++i) {
						arr->push_back(make_default_for_type(*base));
					}
				}
				return std::make_shared<Value>(arr);
			}
			default:
				break;
		}
		throw RuntimeError("unknown type in declaration");
	}

	Value Interpreter::run(const ast::AstTree& tree,
	                       const std::vector<std::string>& script_args) {
		collect_functions(tree);

		auto main_it = functions_.find("main");
		if (main_it == functions_.end()) {
			throw RuntimeError("no 'main' function");
		}

		// argc/argv для скрипта
		int64_t argc = static_cast<int64_t>(script_args.size());

		Array argv_arr = std::make_shared<std::vector<ValuePtr>>();
		argv_arr->reserve(script_args.size());
		for (const auto& s : script_args) {
			argv_arr->push_back(std::make_shared<Value>(s));
		}
		Value argv_val = std::make_shared<Value>(argv_arr);

		// подбираем аргументы по количеству параметров main
		std::vector<Value> args;
		std::size_t idx = 0;
		for_each_child(main_it->second, ast::EdgeRole::Param, [&](ast::ConstNodeIt) {
			if (idx == 0)      args.emplace_back(argc);
			else if (idx == 1) args.emplace_back(argv_val);
			else               args.emplace_back();
			++idx;
		});

		return call_function("main", std::move(args));
	}

	Value Interpreter::call_function(const std::string& name, std::vector<Value> args) {
		if (name == "printf") {
			if (args.empty()) throw RuntimeError("printf: format string expected");
			const auto* fs = std::get_if<std::string>(&args[0].data);
			if (!fs) throw RuntimeError("printf: first argument must be a string");

			std::size_t ai = 1;          // индекс следующего аргумента
			std::string res;

			for (std::size_t i = 0; i < fs->size(); ++i) {
				char c = (*fs)[i];
				if (c != '%' || i + 1 >= fs->size()) { res += c; continue; }
				char spec = (*fs)[++i];
				switch (spec) {
					case '%': res += '%'; break;
					case 'd': {
						if (ai >= args.size()) throw RuntimeError("printf: not enough arguments");
						res += std::to_string(as_int(args[ai++]));
						break;
					}
					case 'f': {
						if (ai >= args.size()) throw RuntimeError("printf: not enough arguments");
						char buf[64];
						std::snprintf(buf, sizeof(buf), "%f", as_double(args[ai++]));
						res += buf;
						break;
					}
					case 'g': {   // компактный вывод: 3.14 вместо 3.140000
						if (ai >= args.size()) throw RuntimeError("printf: not enough arguments");
						char buf[64];
						std::snprintf(buf, sizeof(buf), "%g", as_double(args[ai++]));
						res += buf;
						break;
					}
					case 'c': {
						if (ai >= args.size()) throw RuntimeError("printf: not enough arguments");
						const auto& v = args[ai++];
						if (auto* ch = std::get_if<char>(&v.data))        res += *ch;
						else if (auto* n = std::get_if<int64_t>(&v.data)) res += static_cast<char>(*n);
						else throw RuntimeError("printf: %c expects char");
						break;
					}
					case 's': {
						if (ai >= args.size()) throw RuntimeError("printf: not enough arguments");
						if (auto* s = std::get_if<std::string>(&args[ai++].data)) res += *s;
						else throw RuntimeError("printf: %s expects string");
						break;
					}
					default:
						throw RuntimeError(std::string("printf: unknown format specifier '%") + spec + "'");
				}
			}
			out_ << res;
			return Value{int64_t{0}};
		}
		if (name == "prints" || name == "printi") {
			if (!args.empty()) {
				const auto& v = args[0];
				if (auto* s = std::get_if<std::string>(&v.data)) {
					out_ << *s;
				} else if (auto* n = std::get_if<int64_t>(&v.data)) {
					out_ << *n;
				} else if (auto* c = std::get_if<char>(&v.data)) {
					out_ << *c;
				} else if (auto* b = std::get_if<bool>(&v.data)) {
					out_ << (*b ? "true" : "false");
				} else if (auto* p = std::get_if<ValuePtr>(&v.data)) {
					if (*p) {
						const auto& inner = (*p)->data;
						if (auto* s = std::get_if<std::string>(&inner))       out_ << *s;
						else if (auto* n = std::get_if<int64_t>(&inner))      out_ << *n;
						else if (auto* c = std::get_if<char>(&inner))         out_ << *c;
						else if (auto* b = std::get_if<bool>(&inner))         out_ << (*b ? "true" : "false");
						else if (auto* sa = std::get_if<Array>(&inner)) {
							// печать массива: [v1, v2, ...]
							out_ << "[";
							bool first = true;
							for (const auto& item : **sa) {
								if (!first) out_ << ", ";
								first = false;
								if (auto* sn = std::get_if<int64_t>(&item->data))      out_ << *sn;
								else if (auto* sc = std::get_if<char>(&item->data))    out_ << *sc;
								else if (auto* ss = std::get_if<std::string>(&item->data)) out_ << *ss;
								else if (auto* sb = std::get_if<bool>(&item->data))    out_ << (*sb ? "true" : "false");
							}
							out_ << "]";
						}
					} else {
						out_ << "null";
					}
				}
			}
			return Value{int64_t{0}};
		}

		auto it = functions_.find(name);
		if (it == functions_.end()) {
			throw RuntimeError("call to undefined function '" + name + "'");
		}
		if (call_depth_ >= kMaxCallDepth) {
			throw RuntimeError("call depth exceeded (" +
			                   std::to_string(kMaxCallDepth) + ")");
		}
		++call_depth_;

		auto fn = it->second;
		push_scope();

		Value result;
		try {
			std::size_t i = 0;
			for_each_child(fn, ast::EdgeRole::Param, [&](ast::ConstNodeIt param) {
				const auto& pname = std::get<std::string>(param->data().value);
				Value v = i < args.size() ? args[i] : Value{};
				declare(pname, std::make_shared<Value>(std::move(v)));
				++i;
			});

			try {
				for_each_child(fn, ast::EdgeRole::Body, [this](ast::ConstNodeIt body) {
					exec(body);
				});
			} catch (ReturnSignal& s) {
				result = std::move(s.value);
			}
		} catch (...) {
			pop_scope();
			--call_depth_;
			throw;
		}

		pop_scope();
		--call_depth_;
		return result;
	}
// выражения

	Value Interpreter::eval(ast::ConstNodeIt node) {
		const auto& d = node->data();
		switch (d.kind) {
			case ast::NodeKind::Number:    return Value{std::get<int64_t>(d.value)};
			case ast::NodeKind::FloatLit:  return Value{std::get<double>(d.value)};
			case ast::NodeKind::BoolLit:   return Value{std::get<bool>(d.value)};
			case ast::NodeKind::CharLit:   return Value{std::get<char>(d.value)};
			case ast::NodeKind::StringLit: return Value{std::get<std::string>(d.value)};

			case ast::NodeKind::Identifier: {
				ValuePtr slot = lookup_ptr(std::get<std::string>(d.value));
				return *slot;
			}

			case ast::NodeKind::Binary: {
				auto lhs = child_or_none(node, ast::EdgeRole::Lhs);
				auto rhs = child_or_none(node, ast::EdgeRole::Rhs);
				if (!lhs || !rhs) throw RuntimeError("binary missing operand");
				return eval_binary(std::get<ast::BinaryOp>(d.value), eval(*lhs), eval(*rhs));
			}

			case ast::NodeKind::Unary: {
				auto op = std::get<ast::UnaryOp>(d.value);
				auto operand = child_or_none(node, ast::EdgeRole::Operand);
				if (!operand) throw RuntimeError("unary missing operand");

				// & требует lvalue — берём адрес слота, а не значение
				if (op == ast::UnaryOp::Addr) {
					ValuePtr slot = eval_lvalue(*operand);
					return Value{slot};
				}
				return eval_unary(op, eval(*operand));
			}

			case ast::NodeKind::Assign: {
				auto lhs = child_or_none(node, ast::EdgeRole::Lhs);
				auto rhs = child_or_none(node, ast::EdgeRole::Rhs);
				if (!lhs || !rhs) throw RuntimeError("assign missing operand");

				ValuePtr target = eval_lvalue(*lhs);
				Value    rv     = eval(*rhs);
				auto op = std::get<ast::AssignOp>(d.value);

				switch (op) {
					case ast::AssignOp::Assign: *target = rv; break;

					case ast::AssignOp::Add:
					case ast::AssignOp::Sub:
					case ast::AssignOp::Mul:
					case ast::AssignOp::Div:
					case ast::AssignOp::Mod: {
						if (is_float(*target) || is_float(rv)) {
							double a = as_double(*target), b = as_double(rv);
							switch (op) {
								case ast::AssignOp::Add: *target = Value{a + b}; break;
								case ast::AssignOp::Sub: *target = Value{a - b}; break;
								case ast::AssignOp::Mul: *target = Value{a * b}; break;
								case ast::AssignOp::Div:
									if (b == 0.0) throw RuntimeError("division by zero");
									*target = Value{a / b}; break;
								case ast::AssignOp::Mod:
									if (b == 0.0) throw RuntimeError("modulo by zero");
									*target = Value{std::fmod(a, b)}; break;
								default: break;
							}
						} else {
							int64_t a = as_int(*target), b = as_int(rv);
							switch (op) {
								case ast::AssignOp::Add: *target = Value{a + b}; break;
								case ast::AssignOp::Sub: *target = Value{a - b}; break;
								case ast::AssignOp::Mul: *target = Value{a * b}; break;
								case ast::AssignOp::Div:
									if (b == 0) throw RuntimeError("division by zero");
									*target = Value{a / b}; break;
								case ast::AssignOp::Mod:
									if (b == 0) throw RuntimeError("modulo by zero");
									*target = Value{a % b}; break;
								default: break;
							}
						}
						break;
					}

					case ast::AssignOp::BitAnd:
					case ast::AssignOp::BitOr:
					case ast::AssignOp::BitXor:
					case ast::AssignOp::Shl:
					case ast::AssignOp::Shr:
						if (is_float(*target) || is_float(rv))
							throw RuntimeError("bitwise operation on float value");
						switch (op) {
							case ast::AssignOp::BitAnd: *target = Value{as_int(*target) &  as_int(rv)}; break;
							case ast::AssignOp::BitOr:  *target = Value{as_int(*target) |  as_int(rv)}; break;
							case ast::AssignOp::BitXor: *target = Value{as_int(*target) ^  as_int(rv)}; break;
							case ast::AssignOp::Shl:    *target = Value{as_int(*target) << as_int(rv)}; break;
							case ast::AssignOp::Shr:    *target = Value{as_int(*target) >> as_int(rv)}; break;
							default: break;
						}
						break;
				}
				return *target;
			}

			case ast::NodeKind::Call: {
				auto callee = child_or_none(node, ast::EdgeRole::Callee);
				if (!callee || callee->operator*().data().kind != ast::NodeKind::Identifier) {
					throw RuntimeError("call target must be an identifier");
				}
				const std::string& fname =
						std::get<std::string>(callee->operator*().data().value);
				std::vector<Value> args;
				for_each_child(node, ast::EdgeRole::Arg, [&](ast::ConstNodeIt a) {
					args.push_back(eval(a));
				});
				return call_function(fname, std::move(args));
			}

			case ast::NodeKind::Index: {
				auto base = child_or_none(node, ast::EdgeRole::Base);
				auto sub  = child_or_none(node, ast::EdgeRole::Subscript);
				if (!base || !sub) throw RuntimeError("index missing operand");

				Value base_v = eval(*base);
				int64_t i    = as_int(eval(*sub));

				// строка: индексация даёт char по значению
				if (auto* s = std::get_if<std::string>(&base_v.data)) {
					if (i < 0 || i >= static_cast<int64_t>(s->size())) {
						throw RuntimeError("string index out of range");
					}
					return Value{static_cast<char>((*s)[static_cast<std::size_t>(i)])};
				}

				// массив или указатель на массив: берём lvalue и разыменовываем
				return *eval_lvalue(node);
			}

			default:
				throw RuntimeError("unsupported expression kind");
		}
	}

	ValuePtr Interpreter::eval_lvalue(ast::ConstNodeIt node) {
		const auto& d = node->data();
		switch (d.kind) {
			case ast::NodeKind::Identifier:
				return lookup_ptr(std::get<std::string>(d.value));

			case ast::NodeKind::Unary: {
				auto op = std::get<ast::UnaryOp>(d.value);
				if (op != ast::UnaryOp::Deref) {
					throw RuntimeError("expression is not an lvalue");
				}
				auto operand = child_or_none(node, ast::EdgeRole::Operand);
				if (!operand) throw RuntimeError("deref missing operand");
				Value v = eval(*operand);
				if (auto* p = std::get_if<ValuePtr>(&v.data)) {
					if (!*p) throw RuntimeError("null pointer dereference");
					return *p;
				}
				throw RuntimeError("dereference of non-pointer");
			}

			case ast::NodeKind::Index: {
				auto base = child_or_none(node, ast::EdgeRole::Base);
				auto sub  = child_or_none(node, ast::EdgeRole::Subscript);
				if (!base || !sub) throw RuntimeError("index missing operand");

				Value base_v = eval(*base);
				int64_t i    = as_int(eval(*sub));

				Array arr;
				if (auto* p = std::get_if<ValuePtr>(&base_v.data)) {
					// указатель: разыменовываем и ждём, что там массив
					if (!*p) throw RuntimeError("null pointer dereference");
					auto* a = std::get_if<Array>(&(*p)->data);
					if (!a) throw RuntimeError("cannot index non-array pointer");
					arr = *a;
				} else if (auto* a = std::get_if<Array>(&base_v.data)) {
					arr = *a;
				} else {
					throw RuntimeError("cannot index value of this type");
				}

				if (!arr) throw RuntimeError("null array");
				if (i < 0 || i >= static_cast<int64_t>(arr->size())) {
					throw RuntimeError("array index out of range");
				}
				return (*arr)[static_cast<std::size_t>(i)];
			}

			default:
				throw RuntimeError("expression is not an lvalue");
		}
	}

// инструкции

	void Interpreter::exec(ast::ConstNodeIt node) {
		switch (node->data().kind) {
			case ast::NodeKind::Block: {
				push_scope();
				try {
					for_each_child(node, ast::EdgeRole::Stmt, [this](ast::ConstNodeIt s) {
						exec(s);
					});
				} catch (...) {
					pop_scope();
					throw;
				}
				pop_scope();
				break;
			}

			case ast::NodeKind::VarDecl: {
				const auto& name = std::get<std::string>(node->data().value);

				auto decl_type = child_or_none(node, ast::EdgeRole::DeclType);
				if (!decl_type) throw RuntimeError("variable without type");

				ValuePtr slot = make_default_for_type(*decl_type);

				// инициализация (если есть)
				if (auto init = child_or_none(node, ast::EdgeRole::Init)) {
					if (std::holds_alternative<Array>(slot->data)) {
						throw RuntimeError("cannot initialize array '" + name +
						                   "' with an expression");
					}
					*slot = eval(*init);
				}
				declare(name, std::move(slot));
				break;
			}

			case ast::NodeKind::ExprStmt:
				for_each_child(node, ast::EdgeRole::Expr, [this](ast::ConstNodeIt e) {
					(void)eval(e);
				});
				break;

			case ast::NodeKind::If:
			case ast::NodeKind::Elif: {
				auto cond = child_or_none(node, ast::EdgeRole::Cond);
				auto then = child_or_none(node, ast::EdgeRole::Then);
				auto elif = child_or_none(node, ast::EdgeRole::ElifBranch);
				if (!cond || !then) throw RuntimeError("malformed if/elif");
				if (as_bool(eval(*cond))) {
					exec(*then);
				} else if (elif) {
					exec(*elif);
				}
				break;
			}

			case ast::NodeKind::Else: {
				if (auto then = child_or_none(node, ast::EdgeRole::Then)) {
					exec(*then);
				}
				break;
			}

			case ast::NodeKind::While: {
				auto cond = child_or_none(node, ast::EdgeRole::Cond);
				auto body = child_or_none(node, ast::EdgeRole::LoopBody);
				if (!cond || !body) throw RuntimeError("malformed while");
				std::size_t iters = 0;
				while (as_bool(eval(*cond))) {
					if (++iters > kMaxLoopIterations) {
						throw RuntimeError("loop iteration limit exceeded (" +
						                   std::to_string(kMaxLoopIterations) + ")");
					}
					exec(*body);
				}
				break;
			}

			case ast::NodeKind::For: {
				auto init = child_or_none(node, ast::EdgeRole::ForInit);
				auto cond = child_or_none(node, ast::EdgeRole::ForCond);
				auto step = child_or_none(node, ast::EdgeRole::ForStep);
				auto body = child_or_none(node, ast::EdgeRole::LoopBody);
				if (!cond || !body) throw RuntimeError("malformed for");
				if (init) (void)eval(*init);
				std::size_t iters = 0;
				while (as_bool(eval(*cond))) {
					if (++iters > kMaxLoopIterations) {
						throw RuntimeError("loop iteration limit exceeded (" +
						                   std::to_string(kMaxLoopIterations) + ")");
					}
					exec(*body);
					if (step) (void)eval(*step);
				}
				break;
			}

			case ast::NodeKind::Return: {
				Value v;
				if (auto e = child_or_none(node, ast::EdgeRole::RetValue)) {
					v = eval(*e);
				}
				throw ReturnSignal{std::move(v)};
			}

			default:
				throw RuntimeError("unsupported statement kind");
		}
	}

// операции

	Value Interpreter::eval_binary(ast::BinaryOp op, const Value& a, const Value& b) {
		if (is_float(a) || is_float(b)) {
			double da = as_double(a), db = as_double(b);
			switch (op) {
				case ast::BinaryOp::Add: return Value{da + db};
				case ast::BinaryOp::Sub: return Value{da - db};
				case ast::BinaryOp::Mul: return Value{da * db};
				case ast::BinaryOp::Div:
					if (db == 0.0) throw RuntimeError("division by zero");
					return Value{da / db};
				case ast::BinaryOp::Mod:
					if (db == 0.0) throw RuntimeError("modulo by zero");
					return Value{std::fmod(da, db)};

				case ast::BinaryOp::Eq: return Value{da == db};
				case ast::BinaryOp::Ne: return Value{da != db};
				case ast::BinaryOp::Lt: return Value{da <  db};
				case ast::BinaryOp::Gt: return Value{da >  db};
				case ast::BinaryOp::Le: return Value{da <= db};
				case ast::BinaryOp::Ge: return Value{da >= db};

				case ast::BinaryOp::LogAnd: return Value{as_bool(a) && as_bool(b)};
				case ast::BinaryOp::LogOr:  return Value{as_bool(a) || as_bool(b)};

				default:
					throw RuntimeError("bitwise operation on float value");
			}
		}
		switch (op) {
			case ast::BinaryOp::Eq: return Value{as_int(a) == as_int(b)};
			case ast::BinaryOp::Ne: return Value{as_int(a) != as_int(b)};
			case ast::BinaryOp::Lt: return Value{as_int(a) <  as_int(b)};
			case ast::BinaryOp::Gt: return Value{as_int(a) >  as_int(b)};
			case ast::BinaryOp::Le: return Value{as_int(a) <= as_int(b)};
			case ast::BinaryOp::Ge: return Value{as_int(a) >= as_int(b)};

			case ast::BinaryOp::LogAnd: return Value{as_bool(a) && as_bool(b)};
			case ast::BinaryOp::LogOr:  return Value{as_bool(a) || as_bool(b)};

			case ast::BinaryOp::Add: return Value{as_int(a) + as_int(b)};
			case ast::BinaryOp::Sub: return Value{as_int(a) - as_int(b)};
			case ast::BinaryOp::Mul: return Value{as_int(a) * as_int(b)};
			case ast::BinaryOp::Div: {
				int64_t div = as_int(b);
				if (div == 0) throw RuntimeError("division by zero");
				return Value{as_int(a) / div};
			}
			case ast::BinaryOp::Mod: {
				int64_t div = as_int(b);
				if (div == 0) throw RuntimeError("modulo by zero");
				return Value{as_int(a) % div};
			}

			case ast::BinaryOp::BitAnd: return Value{as_int(a) &  as_int(b)};
			case ast::BinaryOp::BitOr:  return Value{as_int(a) |  as_int(b)};
			case ast::BinaryOp::BitXor: return Value{as_int(a) ^  as_int(b)};
			case ast::BinaryOp::Shl:    return Value{as_int(a) << as_int(b)};
			case ast::BinaryOp::Shr:    return Value{as_int(a) >> as_int(b)};
		}
		throw RuntimeError("unsupported binary operator");
	}

	Value Interpreter::eval_unary(ast::UnaryOp op, const Value& a) {
		switch (op) {
			case ast::UnaryOp::Neg:
				if (is_float(a)) return Value{-as_double(a)};
				return Value{-as_int(a)};
			case ast::UnaryOp::Not:    return Value{!as_bool(a)};
			case ast::UnaryOp::BitNot:
				if (is_float(a)) throw RuntimeError("bitwise not on float value");
				return Value{~as_int(a)};
			case ast::UnaryOp::Deref: {
				// разыменование как rvalue: если это указатель — вернуть содержимое
				if (auto* p = std::get_if<ValuePtr>(&const_cast<Value&>(a).data)) {
					if (!*p) throw RuntimeError("null pointer dereference");
					return **p;
				}
				throw RuntimeError("dereference of non-pointer");
			}
			case ast::UnaryOp::Addr:
				// обрабатывается в eval() через eval_lvalue; сюда попадать не должна
				throw RuntimeError("'&' must be handled as lvalue");
		}
		throw RuntimeError("unsupported unary operator");
	}

} // namespace interp