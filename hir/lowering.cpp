#include "lowering.hpp"
#include "errors.hpp"

#include <optional>
#include <unordered_map>

namespace hir {
namespace {
using Node = ast::ConstNodeIt;
using Role = ast::EdgeRole;
using Kind = ast::NodeKind;
std::optional<Node> child(Node n, Role role) {
    for (auto it = n->out_begin(); it != n->out_end(); ++it)
        if ((*it)->data() == role) return (*it)->next_node();
    return {};
}
[[noreturn]] void fail(Node n, const std::string& message) {
    throw err::CompileError(err::Stage::Semantic, n->data().loc, message);
}
Node required(Node n, Role role) {
    if (auto c = child(n, role)) return *c;
    fail(n, "malformed AST: missing child");
}
template<class F> void children(Node n, Role role, F&& visit) {
    for (auto it = n->out_begin(); it != n->out_end(); ++it)
        if ((*it)->data() == role) visit((*it)->next_node());
}
const std::string& name(Node n) {
    const auto* value = std::get_if<std::string>(&n->data().value);
    if (!value) fail(n, "malformed AST: missing name");
    return *value;
}
class Lowerer {
    TranslationUnit unit_;
    Function* f_ = nullptr;
    std::unordered_map<std::string, FunctionId> functions_;
    std::vector<std::unordered_map<std::string, ObjectId>> scopes_;
    std::size_t next_label_ = 0;
    std::size_t depth_ = 0;
    struct Depth {
        Lowerer& owner;
        Depth(Lowerer& o, Node n) : owner(o) {
            if (o.depth_ >= 256) fail(n, "AST lowering recursion depth exceeded (256)");
            ++o.depth_;
        }
        ~Depth() { --owner.depth_; }
    };
    struct LValue { ObjectId id; bool indirect; };

    Type type(Node n) {
        Depth guard(*this, n);
        if (n->data().kind == Kind::TypePtr) return Type::pointer(type(required(n, Role::Base)));
        if (n->data().kind == Kind::TypeArray) {
            std::size_t size = 0;
            if (auto count = child(n, Role::ArrSize)) {
                auto number = std::get<int64_t>((*count)->data().value);
                if (number < 0) fail(n, "negative array size");
                size = static_cast<std::size_t>(number);
            }
            auto base = type(required(n, Role::Base));
            if (!base.is_value()) fail(n, "array element must have a value type");
            return Type::array(std::move(base), size);
        }
        if (n->data().kind != Kind::TypePrim) fail(n, "unsupported AST type");
        switch (std::get<ast::TypeKind>(n->data().value)) {
        case ast::TypeKind::Int: return Type::int_type();
        case ast::TypeKind::Float: return Type::float_type();
        case ast::TypeKind::Bool: return Type::bool_type();
        case ast::TypeKind::Char: return Type::char_type();
        case ast::TypeKind::Void: return Type::void_type();
        }
        fail(n, "invalid primitive type");
    }
    const Type& object_type(ObjectId id) const { return unit_.object(*f_, id).type; }
    void emit(Opcode code, Operand a, Operand b, Operand c, Node n) {
        f_->operations.push_back(op(code, std::move(a), std::move(b), std::move(c), n->data().loc));
    }
    LabelId fresh_label() { return LabelId{next_label_++}; }
    void label(LabelId id, Node n) { emit(Opcode::Label, id, {}, {}, n); }
    void jump(LabelId id, Node n) {
        if (!f_->operations.empty() && (f_->operations.back().opcode == Opcode::Call || f_->operations.back().opcode == Opcode::CallIndirect))
            label(fresh_label(), n);
        if (f_->operations.empty() || !is_terminator(f_->operations.back().opcode)) emit(Opcode::Jump, id, {}, {}, n);
    }
    ObjectId temp(Type t) { return f_->add_object("t" + std::to_string(f_->objects.size()), std::move(t)); }
    ObjectId constant(Type t, ConstantValue value, Node n) {
        auto id = temp(t);
        emit(Opcode::Constant, id, Constant{std::move(t), std::move(value)}, {}, n);
        return id;
    }
    std::optional<ObjectId> find_object(const std::string& key) const {
        for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s)
            if (auto it = s->find(key); it != s->end()) return it->second;
        return {};
    }
    ObjectId lookup(Node n) {
        if (auto id = find_object(name(n))) return *id;
        fail(n, "use of undeclared variable '" + name(n) + "'");
    }
    ObjectId convert(ObjectId id, const Type& target, Node n) {
        if (object_type(id) == target) return id;
        if (object_type(id) != Type::int_type() || target != Type::float_type())
            fail(n, "cannot implicitly convert '" + object_type(id).str() + "' to '" + target.str() + "'");
        auto result = temp(target);
        emit(Opcode::Convert, result, id, {}, n);
        return result;
    }
    ObjectId value(Node n) {
        auto result = expression(n);
        if (!result) fail(n, "void expression used as a value");
        return *result;
    }
    Type lvalue_type(LValue l) const {
        if (!l.indirect) return object_type(l.id);
        return *object_type(l.id).element;
    }
    ObjectId read(LValue l, Node n) {
        auto t = lvalue_type(l);
        if (!l.indirect && t.kind == TypeKind::Array) return l.id;
        auto result = temp(t);
        emit(l.indirect ? Opcode::Load : Opcode::Copy, result, l.id, {}, n);
        return result;
    }
    void write(LValue l, ObjectId rhs, Node n) {
        if (lvalue_type(l).kind == TypeKind::Array) fail(n, "array assignment is not supported by the source language");
        rhs = convert(rhs, lvalue_type(l), n);
        emit(l.indirect ? Opcode::Store : Opcode::Copy, l.id, rhs, {}, n);
    }
    LValue lvalue(Node n) {
        Depth guard(*this, n);
        if (n->data().kind == Kind::Identifier) return {lookup(n), false};
        if (n->data().kind == Kind::Unary && std::get<ast::UnaryOp>(n->data().value) == ast::UnaryOp::Deref) {
            auto pointer = value(required(n, Role::Operand));
            const auto t = object_type(pointer);
            if (t.kind != TypeKind::Pointer || !t.element || !t.element->is_value()) fail(n, "cannot dereference this type");
            return {pointer, true};
        }
        if (n->data().kind == Kind::Index) {
            auto base = value(required(n, Role::Base));
            auto index = value(required(n, Role::Subscript));
            const auto t = object_type(base);
            if ((t.kind != TypeKind::Array && t.kind != TypeKind::Pointer) || !t.element || !t.element->is_value())
                fail(n, "index base must be an array or pointer");
            if (object_type(index) != Type::int_type()) fail(n, "index must have type i32");
            auto address = temp(Type::pointer(*t.element));
            emit(Opcode::ElementAddress, address, base, index, n);
            return {address, true};
        }
        fail(n, "expression is not assignable");
    }
    ObjectId binary(Opcode code, ObjectId left, ObjectId right, Node n) {
        const bool comparison = code >= Opcode::Eq && code <= Opcode::Ge;
        const bool integer = code == Opcode::Mod || (code >= Opcode::BitAnd && code <= Opcode::Shr);
        auto lt = object_type(left), rt = object_type(right);
        if (lt.is_numeric() && rt.is_numeric()) {
            auto common = lt.kind == TypeKind::Float || rt.kind == TypeKind::Float ? Type::float_type() : Type::int_type();
            if (integer && common != Type::int_type()) fail(n, "bitwise/modulo operation requires i32 operands");
            left = convert(left, common, n); right = convert(right, common, n); lt = common; rt = common;
        } else if (!comparison || lt != rt || !lt.is_scalar()) fail(n, "incompatible binary operand types");
        if (lt.kind == TypeKind::Pointer && code != Opcode::Eq && code != Opcode::Ne)
            fail(n, "pointers support only equality comparisons");
        auto result = temp(comparison ? Type::bool_type() : lt);
        emit(code, result, left, right, n);
        return result;
    }
    static Opcode binary_code(ast::BinaryOp code) {
        switch (code) {
#define MAP(a,b) case ast::BinaryOp::a: return Opcode::b
        MAP(Add, Add); MAP(Sub, Sub); MAP(Mul, Mul); MAP(Div, Div); MAP(Mod, Mod);
        MAP(BitAnd, BitAnd); MAP(BitOr, BitOr); MAP(BitXor, BitXor); MAP(Shl, Shl); MAP(Shr, Shr);
        MAP(Eq, Eq); MAP(Ne, Ne); MAP(Lt, Lt); MAP(Gt, Gt); MAP(Le, Le); MAP(Ge, Ge);
#undef MAP
        default: throw ValidationError("logical operation must be lowered as control flow");
        }
    }
    static Opcode assign_code(ast::AssignOp code) {
        switch (code) {
#define MAP(a) case ast::AssignOp::a: return Opcode::a
        MAP(Add); MAP(Sub); MAP(Mul); MAP(Div); MAP(Mod); MAP(BitAnd); MAP(BitOr); MAP(BitXor); MAP(Shl); MAP(Shr);
#undef MAP
        default: throw ValidationError("plain assignment has no binary opcode");
        }
    }
    ObjectId logical(Node n, ast::BinaryOp code) {
        auto left = value(required(n, Role::Lhs));
        if (object_type(left) != Type::bool_type()) fail(n, "logical operands must be bool");
        auto right_label = fresh_label(), skip_label = fresh_label(), end = fresh_label();
        auto result = temp(Type::bool_type());
        bool is_and = code == ast::BinaryOp::LogAnd;
        emit(Opcode::Branch, left, is_and ? right_label : skip_label, is_and ? skip_label : right_label, n);
        label(skip_label, n);
        emit(Opcode::Constant, result, Constant{Type::bool_type(), !is_and}, {}, n);
        jump(end, n);
        label(right_label, n);
        auto right = value(required(n, Role::Rhs));
        if (object_type(right) != Type::bool_type()) fail(n, "logical operands must be bool");
        emit(Opcode::Copy, result, right, {}, n); jump(end, n);
        label(end, n);
        return result;
    }
    std::optional<ObjectId> expression(Node n) {
        Depth guard(*this, n);
        const auto& d = n->data();
        switch (d.kind) {
        case Kind::Number: return constant(Type::int_type(), std::get<int64_t>(d.value), n);
        case Kind::FloatLit: return constant(Type::float_type(), std::get<double>(d.value), n);
        case Kind::BoolLit: return constant(Type::bool_type(), std::get<bool>(d.value), n);
        case Kind::CharLit: return constant(Type::char_type(), std::get<char>(d.value), n);
        case Kind::StringLit: return constant(Type::pointer(Type::char_type()), std::get<std::string>(d.value), n);
        case Kind::Identifier: case Kind::Index: return read(lvalue(n), n);
        case Kind::Binary: {
            auto code = std::get<ast::BinaryOp>(d.value);
            if (code == ast::BinaryOp::LogAnd || code == ast::BinaryOp::LogOr) return logical(n, code);
            auto left = value(required(n, Role::Lhs));
            auto right = value(required(n, Role::Rhs));
            return binary(binary_code(code), left, right, n);
        }
        case Kind::Unary: {
            auto code = std::get<ast::UnaryOp>(d.value);
            if (code == ast::UnaryOp::Deref) return read(lvalue(n), n);
            auto operand = required(n, Role::Operand);
            if (code == ast::UnaryOp::Addr) {
                auto l = lvalue(operand);
                if (l.indirect) return l.id;
                auto address = temp(Type::pointer(object_type(l.id)));
                emit(Opcode::AddressOf, address, l.id, {}, n); return address;
            }
            auto id = value(operand);
            auto t = object_type(id);
            if (code == ast::UnaryOp::Neg ? !t.is_numeric() :
                t.kind != (code == ast::UnaryOp::Not ? TypeKind::Bool : TypeKind::Int)) fail(n, "invalid unary operand type");
            auto result = temp(t);
            emit(code == ast::UnaryOp::Neg ? Opcode::Neg : code == ast::UnaryOp::Not ? Opcode::LogicalNot : Opcode::BitNot,
                 result, id, {}, n);
            return result;
        }
        case Kind::Assign: {
            auto target = lvalue(required(n, Role::Lhs));
            auto code = std::get<ast::AssignOp>(d.value);
            std::optional<ObjectId> old;
            if (code != ast::AssignOp::Assign) old = read(target, n);
            auto rhs = value(required(n, Role::Rhs));
            if (old) rhs = binary(assign_code(code), *old, rhs, n);
            rhs = convert(rhs, lvalue_type(target), n);
            write(target, rhs, n);
            return rhs;
        }
        case Kind::Call: {
            auto callee = required(n, Role::Callee);
            if (callee->data().kind != Kind::Identifier) fail(n, "source language calls require a function name");
            auto key = name(callee);
            if (find_object(key)) fail(n, "called object '" + key + "' is not a function");
            if (!functions_.contains(key)) fail(n, "call to undeclared function '" + key + "'");
            auto id = functions_.at(key);
            const auto& fn = unit_.functions[id.index];
            std::vector<ObjectId> args;
            children(n, Role::Arg, [&](Node a) {
                auto v = value(a);
                if (args.size() < fn.parameter_types.size()) v = convert(v, fn.parameter_types[args.size()], a);
                args.push_back(v);
            });
            if (args.size() < fn.parameter_types.size() || (!fn.variadic && args.size() != fn.parameter_types.size()) ||
                (fn.builtin == Builtin::Print && args.size() != 1)) fail(n, "wrong number of arguments in call to '" + key + "'");
            std::optional<ObjectId> result;
            if (fn.result.kind != TypeKind::Void) result = temp(fn.result);
            auto call = op(Opcode::Call, result ? Operand{*result} : Operand{}, id, {}, d.loc);
            call.arguments = std::move(args); f_->operations.push_back(std::move(call));
            return result;
        }
        default: fail(n, "unsupported AST expression");
        }
    }
    void condition(Node n, LabelId yes, LabelId no) {
        auto id = value(n);
        if (object_type(id) != Type::bool_type()) fail(n, "condition must be bool");
        emit(Opcode::Branch, id, yes, no, n);
    }
    void statement(Node n) {
        Depth guard(*this, n);
        switch (n->data().kind) {
        case Kind::Block:
            scopes_.emplace_back();
            children(n, Role::Stmt, [&](Node s) {
                if (f_->operations.empty() || !is_terminator(f_->operations.back().opcode) ||
                    f_->operations.back().opcode == Opcode::Call || f_->operations.back().opcode == Opcode::CallIndirect) statement(s);
            });
            scopes_.pop_back(); break;
        case Kind::VarDecl: {
            auto t = type(required(n, Role::DeclType));
            if (!t.is_value()) fail(n, "variable cannot have void type");
            if (scopes_.back().contains(name(n))) fail(n, "redefinition of variable '" + name(n) + "'");
            // The old interpreter zero-initializes declarations without initializers.
            // Emit that initialization explicitly, including for arrays and pointers.
            auto id = f_->add_object(name(n), t, Storage::Local);
            if (auto init = child(n, Role::Init)) {
                if (t.kind == TypeKind::Array) fail(n, "array initializer expressions are not supported");
                auto rhs = convert(value(*init), t, *init);
                emit(Opcode::Copy, id, rhs, {}, n);
            } else emit(Opcode::Constant, id, Constant{t, std::monostate{}}, {}, n);
            scopes_.back().emplace(name(n), id); break;
        }
        case Kind::ExprStmt: expression(required(n, Role::Expr)); break;
        case Kind::Return: {
            if (auto rv = child(n, Role::RetValue)) {
                auto result = expression(*rv);
                if (f_->result.kind == TypeKind::Void) {
                    if (result) fail(n, "void function cannot return a value");
                    emit(Opcode::Return, {}, {}, {}, n);
                } else {
                    if (!result) fail(n, "non-void function must return a value");
                    auto converted = convert(*result, f_->result, n);
                    emit(Opcode::Return, converted, {}, {}, n);
                }
            } else {
                if (f_->result.kind != TypeKind::Void) fail(n, "non-void function must return a value");
                emit(Opcode::Return, {}, {}, {}, n);
            }
            break;
        }
        case Kind::If: case Kind::Elif: {
            auto yes = fresh_label(), no = fresh_label(), end = fresh_label();
            condition(required(n, Role::Cond), yes, no);
            label(yes, n); statement(required(n, Role::Then)); jump(end, n);
            label(no, n);
            if (auto otherwise = child(n, Role::ElifBranch)) statement(*otherwise);
            jump(end, n); label(end, n); break;
        }
        case Kind::Else: statement(required(n, Role::Then)); break;
        case Kind::While: case Kind::For: {
            bool is_for = n->data().kind == Kind::For;
            scopes_.emplace_back();
            if (is_for) expression(required(n, Role::ForInit));
            auto header = fresh_label(), body = fresh_label(), end = fresh_label();
            // A call is a block terminator but execution continues afterwards.
            if (!f_->operations.empty() && (f_->operations.back().opcode == Opcode::Call || f_->operations.back().opcode == Opcode::CallIndirect))
                label(fresh_label(), n);
            jump(header, n); label(header, n);
            condition(required(n, is_for ? Role::ForCond : Role::Cond), body, end);
            label(body, n); statement(required(n, Role::LoopBody));
            if (f_->operations.empty() || !is_terminator(f_->operations.back().opcode) || f_->operations.back().opcode == Opcode::Call) {
                if (is_for) expression(required(n, Role::ForStep));
                if (!f_->operations.empty() && f_->operations.back().opcode == Opcode::Call) label(fresh_label(), n);
                jump(header, n);
            }
            label(end, n); scopes_.pop_back(); break;
        }
        default: fail(n, "unsupported AST statement");
        }
    }
    void add_builtin(std::string key, Builtin kind) {
        Function fn;
        fn.name = key; fn.result = Type::int_type(); fn.variadic = true; fn.external = true; fn.builtin = kind;
        if (kind == Builtin::Printf) fn.parameter_types.push_back(Type::pointer(Type::char_type()));
        functions_.emplace(std::move(key), FunctionId{unit_.functions.size()});
        unit_.functions.push_back(std::move(fn));
    }
public:
    TranslationUnit run(const ast::AstTree& tree) {
        if (!tree.check() || tree.begin() == tree.end()) throw ValidationError("AST is not a non-empty tree");
        auto root = tree.root();
        if (root->data().kind != Kind::Program) fail(root, "AST root is not a program");
        add_builtin("prints", Builtin::Print); add_builtin("printi", Builtin::Print); add_builtin("printf", Builtin::Printf);
        // Register complete signatures first, so forward and mutually recursive calls work.
        children(root, Role::Func, [&](Node n) {
            Function fn;
            fn.name = name(n); fn.result = type(required(n, Role::ReturnType));
            children(n, Role::Param, [&](Node p) {
                auto t = type(required(p, Role::DeclType));
                if (!t.is_value()) fail(p, "parameter cannot have void type");
                fn.parameter_types.push_back(t);
                fn.add_object(name(p), std::move(t), Storage::Parameter);
            });
            if (!functions_.emplace(fn.name, FunctionId{unit_.functions.size()}).second) fail(n, "duplicate function '" + fn.name + "'");
            unit_.functions.push_back(std::move(fn));
        });
        children(root, Role::Func, [&](Node n) {
            f_ = &unit_.functions[functions_.at(name(n)).index];
            next_label_ = 0; scopes_.clear(); scopes_.emplace_back();
            for (auto id : f_->parameters)
                if (!scopes_.back().emplace(f_->objects[id.index].name, id).second) fail(n, "duplicate parameter");
            label(fresh_label(), n);
            statement(required(n, Role::Body));
        });
        build_cfg(unit_);
        verify(unit_);
        return std::move(unit_);
    }
};
}
TranslationUnit lower(const ast::AstTree& tree) { return Lowerer{}.run(tree); }
} // namespace hir
