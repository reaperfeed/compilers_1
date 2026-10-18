#include "hir.hpp"

#include <algorithm>
#include <unordered_set>

namespace hir {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw ValidationError(message);
}
bool valid_type(const Type& t, std::size_t depth = 0) {
    if (depth > 64) return false;
    switch (t.kind) {
    case TypeKind::Pointer:
        return t.element && !t.extent && t.name.empty() && t.fields.empty() && t.parameters.empty() && !t.variadic &&
               valid_type(*t.element, depth + 1);
    case TypeKind::Array:
        return t.element && t.element->is_value() && t.name.empty() && t.fields.empty() && t.parameters.empty() && !t.variadic &&
               valid_type(*t.element, depth + 1);
    case TypeKind::Struct: {
        if (t.element || t.extent || !t.parameters.empty() || t.variadic) return false;
        std::unordered_set<std::string> names;
        for (const auto& [name, field] : t.fields)
            if (name.empty() || !names.insert(name).second || !field.is_value() || !valid_type(field, depth + 1)) return false;
        return !t.name.empty();
    }
    case TypeKind::Function:
        if (!t.element || t.extent || !t.name.empty() || !t.fields.empty() ||
            t.element->kind == TypeKind::Function || !valid_type(*t.element, depth + 1)) return false;
        for (const auto& p : t.parameters) if (!p.is_value() || !valid_type(p, depth + 1)) return false;
        return true;
    case TypeKind::Void: case TypeKind::Int: case TypeKind::Float: case TypeKind::Bool: case TypeKind::Char:
        return !t.element && !t.extent && t.name.empty() && t.fields.empty() && t.parameters.empty() && !t.variadic;
    }
    return false;
}
bool valid_constant(const Constant& c) {
    if (!valid_type(c.type) || !c.type.is_value()) return false;
    if (std::holds_alternative<std::monostate>(c.value)) return true;
    switch (c.type.kind) {
    case TypeKind::Int: return std::holds_alternative<int64_t>(c.value);
    case TypeKind::Float: return std::holds_alternative<double>(c.value);
    case TypeKind::Bool: return std::holds_alternative<bool>(c.value);
    case TypeKind::Char: return std::holds_alternative<char>(c.value);
    case TypeKind::Pointer:
        return c.type.element && *c.type.element == Type::char_type() && std::holds_alternative<std::string>(c.value);
    default: return false;
    }
}
void verify_operation(const TranslationUnit& unit, const Function& f, const Operation& o, std::size_t index) {
    const std::string where = f.name + ": operation " + std::to_string(index) + " (" + opcode_name(o.opcode) + ")" +
        (o.loc.line ? " at " + std::to_string(o.loc.line) + ":" + std::to_string(o.loc.column) : "") + ": ";
    auto check = [&](bool c, const std::string& message) { require(c, where + message); };
    auto object = [&](std::size_t i) -> const Object& {
        const auto* id = std::get_if<ObjectId>(&o.args[i]);
        check(id != nullptr, "expected object operand " + std::to_string(i));
        const auto& objects = id->global ? unit.globals : f.objects;
        check(id->index < objects.size(), "object index out of bounds");
        return objects[id->index];
    };
    auto type = [&](std::size_t i) -> const Type& { return object(i).type; };
    auto shape = [&](std::size_t arity) {
        for (std::size_t i = arity; i < 3; ++i)
            check(std::holds_alternative<std::monostate>(o.args[i]), "unexpected fixed operand");
        check(o.arguments.empty(), "unexpected variable argument list");
    };
    auto label = [&](std::size_t i) { check(std::holds_alternative<LabelId>(o.args[i]), "expected label"); };
    auto same = [&](std::size_t a, std::size_t b) { check(type(a) == type(b), "operand types must be identical"); };
    auto binary = [&](bool integer, bool comparison) {
        shape(3); same(1, 2);
        if (comparison) {
            check(type(0) == Type::bool_type(), "comparison result must be bool");
            check(type(1).is_scalar(), "comparison requires scalar operands");
            if (type(1).kind == TypeKind::Pointer)
                check(o.opcode == Opcode::Eq || o.opcode == Opcode::Ne, "pointers support only equality comparisons");
        } else {
            same(0, 1);
            check(integer ? type(0).kind == TypeKind::Int : type(0).is_numeric(), "invalid arithmetic operand type");
        }
    };
    switch (o.opcode) {
    case Opcode::Constant: {
        shape(2);
        const auto* c = std::get_if<Constant>(&o.args[1]);
        check(c && valid_constant(*c), "constant has invalid type or value");
        check(type(0) == c->type, "constant type does not match destination"); break;
    }
    case Opcode::Copy: shape(2); same(0, 1); break;
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Div: binary(false, false); break;
    case Opcode::Mod: case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor: case Opcode::Shl: case Opcode::Shr:
        binary(true, false); break;
    case Opcode::Eq: case Opcode::Ne: case Opcode::Lt: case Opcode::Gt: case Opcode::Le: case Opcode::Ge:
        binary(false, true); break;
    case Opcode::Neg: case Opcode::LogicalNot: case Opcode::BitNot:
        shape(2); same(0, 1);
        check(o.opcode == Opcode::Neg ? type(0).is_numeric() :
              type(0).kind == (o.opcode == Opcode::LogicalNot ? TypeKind::Bool : TypeKind::Int), "invalid unary type"); break;
    case Opcode::AddressOf:
        shape(2); check(object(1).storage != Storage::Temporary, "cannot take address of a temporary");
        check(type(0) == Type::pointer(type(1)), "address result type mismatch"); break;
    case Opcode::Load:
        shape(2); check(type(1).kind == TypeKind::Pointer && type(1).element && *type(1).element == type(0), "load type mismatch"); break;
    case Opcode::Store:
        shape(2); check(type(0).kind == TypeKind::Pointer && type(0).element && *type(0).element == type(1), "store type mismatch"); break;
    case Opcode::ElementAddress:
        shape(3);
        check((type(1).kind == TypeKind::Array || type(1).kind == TypeKind::Pointer) && type(1).element &&
              type(1).element->is_value(), "index base must be an array or pointer to a value");
        check(type(2) == Type::int_type(), "array index must be i32");
        check(type(0) == Type::pointer(*type(1).element), "element address type mismatch"); break;
    case Opcode::FieldAddress: {
        shape(3);
        check(type(1).kind == TypeKind::Struct && object(1).storage != Storage::Temporary, "field base must be a struct object");
        const auto* field = std::get_if<FieldIndex>(&o.args[2]);
        check(field && field->index < type(1).fields.size(), "field index out of bounds");
        check(type(0) == Type::pointer(type(1).fields[field->index].second), "field address type mismatch"); break;
    }
    case Opcode::Convert:
        shape(2);
        check(type(0).kind != TypeKind::Pointer && type(1).kind != TypeKind::Pointer && type(0).is_scalar() && type(1).is_scalar(),
              "conversion requires numeric, bool or char types"); break;
    case Opcode::Label: case Opcode::Jump: shape(1); label(0); break;
    case Opcode::Branch: shape(3); check(type(0) == Type::bool_type(), "branch condition must be bool"); label(1); label(2); break;
    case Opcode::Call: case Opcode::CallIndirect: {
        check(std::holds_alternative<std::monostate>(o.args[2]), "unexpected third call operand");
        Type signature;
        const Function* callee = nullptr;
        if (o.opcode == Opcode::Call) {
            const auto* id = std::get_if<FunctionId>(&o.args[1]);
            check(id && id->index < unit.functions.size(), "invalid callee ID");
            callee = &unit.functions[id->index]; signature = callee->signature();
        } else {
            check(type(1).kind == TypeKind::Pointer && type(1).element && type(1).element->kind == TypeKind::Function,
                  "indirect call requires a function pointer");
            signature = *type(1).element;
        }
        if (signature.element->kind == TypeKind::Void)
            check(std::holds_alternative<std::monostate>(o.args[0]), "void call cannot have a destination");
        else check(type(0) == *signature.element, "call result type mismatch");
        check(o.arguments.size() >= signature.parameters.size() &&
              (signature.variadic || o.arguments.size() == signature.parameters.size()), "wrong call argument count");
        if (callee && callee->builtin == Builtin::Print) check(o.arguments.size() == 1, "print requires one argument");
        for (std::size_t i = 0; i < o.arguments.size(); ++i) {
            const auto id = o.arguments[i];
            const auto& objects = id.global ? unit.globals : f.objects;
            check(id.index < objects.size(), "call argument object index out of bounds");
            if (i < signature.parameters.size()) check(objects[id.index].type == signature.parameters[i], "call argument type mismatch");
        }
        break;
    }
    case Opcode::Return:
        shape(1);
        if (f.result.kind == TypeKind::Void) check(std::holds_alternative<std::monostate>(o.args[0]), "void return must have no value");
        else check(type(0) == f.result, "return value type mismatch");
        break;
    case Opcode::Unreachable: shape(0); break;
    default: check(false, "unknown opcode");
    }
}
void verify_definitions(const TranslationUnit& unit, const Function& f) {
    const auto& blocks = f.cfg.blocks;
    const auto n = f.objects.size();
    using Bits = std::vector<bool>;
    Bits parameters(n, false);
    for (auto id : f.parameters) parameters[id.index] = true;
    std::vector<bool> reachable(blocks.size(), false);
    std::vector<std::size_t> work{0};
    while (!work.empty()) {
        auto b = work.back(); work.pop_back();
        if (reachable[b]) continue;
        reachable[b] = true;
        for (const auto& edge : blocks[b].successors) work.push_back(edge.block);
    }
    std::vector<Bits> in(blocks.size(), Bits(n, true)), out = in;
    bool changed;
    do {
        changed = false;
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            if (!reachable[b]) continue;
            Bits before(n, true);
            if (b == 0) before = parameters;
            else for (auto pred : blocks[b].predecessors) {
                if (!reachable[pred]) continue;
                for (std::size_t j = 0; j < n; ++j) before[j] = before[j] && out[pred][j];
            }
            Bits after = before;
            for (auto i = blocks[b].begin; i < blocks[b].end; ++i)
                if (auto id = definition(f.operations[i]); id && !id->global) after[id->index] = true;
            if (in[b] != before || out[b] != after) { in[b] = std::move(before); out[b] = std::move(after); changed = true; }
        }
    } while (changed);
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        if (!reachable[b]) continue;
        Bits defined = in[b];
        for (auto i = blocks[b].begin; i < blocks[b].end; ++i) {
            const auto& o = f.operations[i];
            for (auto id : uses(o, f, unit))
                require(id.global || defined[id.index], f.name + ": operation " + std::to_string(i) +
                        ": value %" + std::to_string(id.index) + " is used before definition on some control-flow path");
            if (auto id = definition(o); id && !id->global) defined[id->index] = true;
        }
    }
}
}
void verify(const TranslationUnit& unit) {
    std::unordered_set<std::string> global_names;
    for (const auto& g : unit.globals) {
        require(!g.name.empty() && global_names.insert(g.name).second, "duplicate/empty global name");
        require(g.storage == Storage::Global && valid_type(g.type) && g.type.is_value(), "invalid global object");
        if (g.initializer) require(valid_constant(*g.initializer) && g.initializer->type == g.type, "invalid global initializer");
    }
    for (const auto& f : unit.functions) {
        require(!f.name.empty() && global_names.insert(f.name).second, "duplicate/empty function name");
        require(valid_type(f.signature()), f.name + ": invalid function signature");
        if (f.builtin != Builtin::None) {
            require(f.external && f.result == Type::int_type() && f.variadic, f.name + ": invalid builtin declaration");
            require(f.builtin == Builtin::Print ? f.parameter_types.empty() : f.parameter_types == std::vector<Type>{Type::pointer(Type::char_type())},
                    f.name + ": invalid builtin parameters");
        }
        if (f.external) {
            require(f.operations.empty() && f.cfg.blocks.empty() && f.objects.empty() && f.parameters.empty(), f.name + ": external function has a body");
            continue;
        }
        require(f.parameters.size() == f.parameter_types.size(), f.name + ": parameter count mismatch");
        std::unordered_set<std::size_t> params;
        for (std::size_t i = 0; i < f.parameters.size(); ++i) {
            auto id = f.parameters[i];
            require(!id.global && id.index < f.objects.size() && params.insert(id.index).second, f.name + ": invalid/duplicate parameter ID");
            require(f.objects[id.index].storage == Storage::Parameter && f.objects[id.index].type == f.parameter_types[i], f.name + ": invalid parameter object");
        }
        for (std::size_t i = 0; i < f.objects.size(); ++i) {
            const auto& v = f.objects[i];
            require(valid_type(v.type) && v.type.is_value() && !v.initializer, f.name + ": invalid local object type/initializer");
            require(v.storage == Storage::Parameter || v.storage == Storage::Local || v.storage == Storage::Temporary, f.name + ": invalid local storage");
            require((v.storage == Storage::Parameter) == params.contains(i), f.name + ": parameter object not in signature");
        }
        for (std::size_t i = 0; i < f.operations.size(); ++i) verify_operation(unit, f, f.operations[i], i);
        // Rebuild from a copy: verification must reject stale/hand-edited CFGs, not repair them.
        auto rebuilt = f;
        build_cfg(rebuilt);
        require(rebuilt.operations.size() == f.operations.size(), f.name + ": linear IR is not normalized");
        require(rebuilt.cfg.blocks == f.cfg.blocks, f.name + ": CFG does not match linear IR");
        require(!f.cfg.blocks.empty(), f.name + ": missing entry block");
        verify_definitions(unit, f);
    }
}
} // namespace hir
