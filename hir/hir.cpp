#include "hir.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace hir {
namespace {
Type primitive(TypeKind kind) { Type t; t.kind = kind; return t; }
std::string object_text(ObjectId id) {
    return std::string(id.global ? "@g" : "%") + std::to_string(id.index);
}
std::string constant_text(const Constant& c) {
    std::ostringstream out;
    out << c.type.str() << ' ';
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) out << "zero";
        else if constexpr (std::is_same_v<T, std::string>) {
            out << '"';
            for (unsigned char ch : value) {
                switch (ch) {
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                case '\0': out << "\\0"; break;
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                default: out << ch;
                }
            }
            out << '"';
        } else if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
        else if constexpr (std::is_same_v<T, char>) out << static_cast<int>(static_cast<unsigned char>(value));
        else out << std::setprecision(17) << value;
    }, c.value);
    return out.str();
}
}

Type Type::void_type() { return primitive(TypeKind::Void); }
Type Type::int_type() { return primitive(TypeKind::Int); }
Type Type::float_type() { return primitive(TypeKind::Float); }
Type Type::bool_type() { return primitive(TypeKind::Bool); }
Type Type::char_type() { return primitive(TypeKind::Char); }
Type Type::pointer(Type base) {
    Type t = primitive(TypeKind::Pointer); t.element = std::make_shared<Type>(std::move(base)); return t;
}
Type Type::array(Type base, std::size_t size) {
    Type t = primitive(TypeKind::Array); t.element = std::make_shared<Type>(std::move(base)); t.extent = size; return t;
}
Type Type::structure(std::string name, std::vector<std::pair<std::string, Type>> fields) {
    Type t = primitive(TypeKind::Struct); t.name = std::move(name); t.fields = std::move(fields); return t;
}
Type Type::function(Type result, std::vector<Type> parameters, bool variadic) {
    Type t = primitive(TypeKind::Function); t.element = std::make_shared<Type>(std::move(result));
    t.parameters = std::move(parameters); t.variadic = variadic; return t;
}
bool Type::operator==(const Type& other) const {
    return kind == other.kind && extent == other.extent && name == other.name &&
           fields == other.fields && parameters == other.parameters && variadic == other.variadic &&
           ((!element && !other.element) || (element && other.element && *element == *other.element));
}
bool Type::is_numeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }
bool Type::is_scalar() const {
    return is_numeric() || kind == TypeKind::Bool || kind == TypeKind::Char || kind == TypeKind::Pointer;
}
bool Type::is_value() const { return kind != TypeKind::Void && kind != TypeKind::Function; }
std::string Type::str() const {
    switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Int: return "i32";
    case TypeKind::Float: return "f32";
    case TypeKind::Bool: return "bool";
    case TypeKind::Char: return "char";
    case TypeKind::Pointer: return "ptr " + (element ? element->str() : "?");
    case TypeKind::Array: return (element ? element->str() : "?") + "[" + std::to_string(extent) + "]";
    case TypeKind::Struct: return "struct " + name;
    case TypeKind::Function: {
        std::string s = "fn(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i) s += ", ";
            s += parameters[i].str();
        }
        if (variadic) s += parameters.empty() ? "..." : ", ...";
        return s + ") -> " + (element ? element->str() : "?");
    }
    }
    return "?";
}
Operation op(Opcode code, Operand a, Operand b, Operand c, ast::SourceLocation loc) {
    return Operation{code, {std::move(a), std::move(b), std::move(c)}, {}, loc};
}
const char* opcode_name(Opcode code) {
    switch (code) {
#define HIR_NAME(code, text) case Opcode::code: return text
    HIR_NAME(Constant, "const"); HIR_NAME(Copy, "copy");
    HIR_NAME(Add, "add"); HIR_NAME(Sub, "sub"); HIR_NAME(Mul, "mul"); HIR_NAME(Div, "div");
    HIR_NAME(Mod, "mod"); HIR_NAME(BitAnd, "and"); HIR_NAME(BitOr, "or"); HIR_NAME(BitXor, "xor");
    HIR_NAME(Shl, "shl"); HIR_NAME(Shr, "shr"); HIR_NAME(Neg, "neg");
    HIR_NAME(LogicalNot, "not"); HIR_NAME(BitNot, "bitnot");
    HIR_NAME(Eq, "eq"); HIR_NAME(Ne, "ne"); HIR_NAME(Lt, "lt"); HIR_NAME(Gt, "gt");
    HIR_NAME(Le, "le"); HIR_NAME(Ge, "ge"); HIR_NAME(AddressOf, "addr");
    HIR_NAME(Load, "load"); HIR_NAME(Store, "store"); HIR_NAME(ElementAddress, "elem_addr");
    HIR_NAME(FieldAddress, "field_addr"); HIR_NAME(Convert, "convert");
    HIR_NAME(Label, "label"); HIR_NAME(Jump, "jump"); HIR_NAME(Branch, "branch");
    HIR_NAME(Call, "call"); HIR_NAME(CallIndirect, "call_indirect");
    HIR_NAME(Return, "return"); HIR_NAME(Unreachable, "unreachable");
#undef HIR_NAME
    }
    return "invalid";
}
bool is_terminator(Opcode code) {
    return code == Opcode::Jump || code == Opcode::Branch || code == Opcode::Call ||
           code == Opcode::CallIndirect || code == Opcode::Return || code == Opcode::Unreachable;
}
std::optional<ObjectId> definition(const Operation& o) {
    switch (o.opcode) {
    case Opcode::Label: case Opcode::Jump: case Opcode::Branch: case Opcode::Store:
    case Opcode::Return: case Opcode::Unreachable: return {};
    default:
        if (auto id = std::get_if<ObjectId>(&o.args[0])) return *id;
        return {};
    }
}
std::vector<ObjectId> uses(const Operation& o, const Function& f, const TranslationUnit& u) {
    std::vector<ObjectId> result;
    auto add = [&](std::size_t i) { if (auto id = std::get_if<ObjectId>(&o.args[i])) result.push_back(*id); };
    switch (o.opcode) {
    case Opcode::Constant: case Opcode::Label: case Opcode::Jump: case Opcode::Unreachable:
    case Opcode::AddressOf: case Opcode::FieldAddress: break;
    case Opcode::Branch: case Opcode::Return: add(0); break;
    case Opcode::Store: add(0); add(1); break;
    case Opcode::Call: break;
    case Opcode::CallIndirect: add(1); break;
    case Opcode::ElementAddress: {
        const auto id = std::get<ObjectId>(o.args[1]);
        if (u.object(f, id).type.kind == TypeKind::Pointer) add(1);
        add(2); break;
    }
    default: add(1); add(2); break;
    }
    result.insert(result.end(), o.arguments.begin(), o.arguments.end());
    return result;
}
ObjectId Function::add_object(std::string name, Type type, Storage storage) {
    ObjectId id{objects.size(), false};
    objects.push_back(Object{std::move(name), std::move(type), storage, {}});
    if (storage == Storage::Parameter) parameters.push_back(id);
    return id;
}
Type Function::signature() const { return Type::function(result, parameter_types, variadic); }
const Object& TranslationUnit::object(const Function& f, ObjectId id) const {
    const auto& objects = id.global ? globals : f.objects;
    if (id.index >= objects.size()) throw ValidationError("object index out of bounds");
    return objects[id.index];
}
ObjectId TranslationUnit::add_global(std::string name, Type type, std::optional<Constant> initializer) {
    ObjectId id{globals.size(), true};
    globals.push_back(Object{std::move(name), std::move(type), Storage::Global, std::move(initializer)});
    return id;
}
std::string operation_text(const Operation& o) {
    std::ostringstream out;
    out << opcode_name(o.opcode);
    bool first = true;
    for (const auto& a : o.args) {
        if (std::holds_alternative<std::monostate>(a)) continue;
        out << (first ? " " : ", "); first = false;
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ObjectId>) out << object_text(v);
            else if constexpr (std::is_same_v<T, Constant>) out << constant_text(v);
            else if constexpr (std::is_same_v<T, LabelId>) out << 'L' << v.index;
            else if constexpr (std::is_same_v<T, FunctionId>) out << "@f" << v.index;
            else if constexpr (std::is_same_v<T, FieldIndex>) out << "field[" << v.index << ']';
        }, a);
    }
    if (o.opcode == Opcode::Call || o.opcode == Opcode::CallIndirect) {
        out << " (";
        for (std::size_t i = 0; i < o.arguments.size(); ++i) {
            if (i) out << ", ";
            out << object_text(o.arguments[i]);
        }
        out << ')';
    }
    return out.str();
}
void dump(const TranslationUnit& unit, std::ostream& out) {
    for (std::size_t i = 0; i < unit.globals.size(); ++i) {
        const auto& g = unit.globals[i];
        out << "global @g" << i << " (" << g.name << "): " << g.type.str();
        if (g.initializer) out << " = " << constant_text(*g.initializer);
        out << '\n';
    }
    for (std::size_t i = 0; i < unit.functions.size(); ++i) {
        const auto& f = unit.functions[i];
        out << (f.external ? "extern " : "function ") << "@f" << i << " (" << f.name << "): " << f.signature().str() << '\n';
        if (f.external) continue;
        for (std::size_t n = 0; n < f.objects.size(); ++n) {
            const auto& v = f.objects[n];
            out << "  " << (v.storage == Storage::Parameter ? "param " : v.storage == Storage::Temporary ? "temp " : "local ")
                << '%' << n << " (" << v.name << "): " << v.type.str() << '\n';
        }
        for (const auto& operation : f.operations) out << "  " << operation_text(operation) << '\n';
        out << '\n';
    }
}
} // namespace hir
