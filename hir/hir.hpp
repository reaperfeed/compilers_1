#pragma once

#include "source_location.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hir {

// Types are structural: no implicit conversions are allowed in operations.
enum class TypeKind { Void, Int, Float, Bool, Char, Pointer, Array, Struct, Function };
struct Type {
    TypeKind kind = TypeKind::Void;
    std::shared_ptr<const Type> element; // pointee, array element or function result
    std::size_t extent = 0;
    std::string name;
    std::vector<std::pair<std::string, Type>> fields;
    std::vector<Type> parameters;
    bool variadic = false;

    static Type void_type();
    static Type int_type();
    static Type float_type();
    static Type bool_type();
    static Type char_type();
    static Type pointer(Type base);
    static Type array(Type base, std::size_t size);
    static Type structure(std::string name, std::vector<std::pair<std::string, Type>> fields);
    static Type function(Type result, std::vector<Type> parameters, bool variadic = false);
    bool operator==(const Type& other) const;
    [[nodiscard]] bool is_numeric() const;
    [[nodiscard]] bool is_scalar() const;
    [[nodiscard]] bool is_value() const;
    [[nodiscard]] std::string str() const;
};

// monostate is a typed zero initializer (including null pointers and aggregates).
using ConstantValue = std::variant<std::monostate, int64_t, double, bool, char, std::string>;
struct Constant {
    Type type;
    ConstantValue value;
};
struct ObjectId {
    std::size_t index = 0;
    bool global = false;
    bool operator==(const ObjectId&) const = default;
};
struct LabelId {
    std::size_t index = 0;
    bool operator==(const LabelId&) const = default;
};
struct FunctionId {
    std::size_t index = 0;
    bool operator==(const FunctionId&) const = default;
};
struct FieldIndex {
    std::size_t index = 0;
    bool operator==(const FieldIndex&) const = default;
};

enum class Storage { Global, Parameter, Local, Temporary };
struct Object {
    std::string name;
    Type type;
    Storage storage = Storage::Local;
    std::optional<Constant> initializer; // only global objects
};

enum class Opcode {
    Constant, Copy,
    Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr,
    Neg, LogicalNot, BitNot,
    Eq, Ne, Lt, Gt, Le, Ge,
    AddressOf, Load, Store, ElementAddress, FieldAddress, Convert,
    Label, Jump, Branch, Call, CallIndirect, Return, Unreachable
};
using Operand = std::variant<std::monostate, ObjectId, Constant, LabelId, FunctionId, FieldIndex>;
struct Operation {
    Opcode opcode = Opcode::Unreachable;
    // dst, lhs, rhs for value operations; ptr, value for store;
    // condition, true label, false label for branch; dst, callee for call.
    std::array<Operand, 3> args{};
    std::vector<ObjectId> arguments;
    ast::SourceLocation loc;
};
Operation op(Opcode code, Operand a = {}, Operand b = {}, Operand c = {},
             ast::SourceLocation loc = {});
const char* opcode_name(Opcode code);
bool is_terminator(Opcode code); // calls terminate a block as well
std::optional<ObjectId> definition(const Operation& operation);
std::vector<ObjectId> uses(const Operation& operation, const struct Function& function,
                           const struct TranslationUnit& unit);

enum class EdgeKind { Jump, True, False, CallContinuation };
struct CFGEdge {
    std::size_t block;
    EdgeKind kind;
    bool operator==(const CFGEdge&) const = default;
};
struct BasicBlock {
    LabelId label;
    std::size_t begin = 0;
    std::size_t end = 0; // half-open range in Function::operations
    std::vector<CFGEdge> successors;
    std::vector<std::size_t> predecessors;
    bool operator==(const BasicBlock&) const = default;
};
struct ControlFlowGraph {
    std::vector<BasicBlock> blocks;
};

enum class Builtin { None, Print, Printf };
struct Function {
    std::string name;
    Type result = Type::void_type();
    std::vector<Type> parameter_types;
    bool variadic = false;
    bool external = false;
    Builtin builtin = Builtin::None;
    std::vector<ObjectId> parameters;
    std::vector<Object> objects;
    std::vector<Operation> operations;
    ControlFlowGraph cfg;

    ObjectId add_object(std::string name, Type type, Storage storage = Storage::Temporary);
    [[nodiscard]] Type signature() const;
};
struct TranslationUnit {
    std::vector<Object> globals;
    std::vector<Function> functions;
    [[nodiscard]] const Object& object(const Function& function, ObjectId id) const;
    ObjectId add_global(std::string name, Type type, std::optional<Constant> initializer = {});
};

class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Normalizes linear IR, inserts missing labels/jumps, then constructs CFG edges.
void build_cfg(Function& function);
void build_cfg(TranslationUnit& unit);
// Checks type/operand/CFG invariants, then definite definitions on every reachable path.
void verify(const TranslationUnit& unit);
void dump(const TranslationUnit& unit, std::ostream& out);
void dump_cfg_dot(const TranslationUnit& unit, std::ostream& out);
std::string operation_text(const Operation& operation);

} // namespace hir
