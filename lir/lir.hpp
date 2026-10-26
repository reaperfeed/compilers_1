#pragma once

#include "hir.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace lir {

// This is a small, target-neutral machine model.  Integer and floating-point
// values occupy one 8-byte word; bool and char occupy one byte; pointers are
// 8 bytes.  It is intentionally an educational IR, not a native assembler.
enum class RegisterKind { Virtual, StackPointer, BasePointer };
struct Register {
    RegisterKind kind = RegisterKind::Virtual;
    std::size_t index = 0;
    bool operator==(const Register&) const = default;
};

struct Memory {
    Register base;
    std::int64_t displacement = 0;
    std::size_t width = 0; // 0 is used by lea; otherwise bytes read/written
    bool operator==(const Memory&) const = default;
};
struct Symbol {
    std::string name;
    bool function = false;
    bool operator==(const Symbol&) const = default;
};
using Operand = std::variant<std::monostate, Register, Memory, std::int64_t, double,
                             bool, char, std::string, hir::LabelId, Symbol>;

struct FrameSlot {
    std::int64_t offset = 0; // signed byte offset from bp; stack locals are negative
    std::size_t size = 0;
    std::size_t alignment = 1;
    bool operator==(const FrameSlot&) const = default;
};
using ObjectLocation = std::variant<Register, FrameSlot>;
struct ObjectPlacement {
    hir::ObjectId object;
    ObjectLocation location;
};

enum class Opcode {
    Push, Pop, Move, LoadImmediate, LoadAddress, Load, Store,
    Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
    Neg, LogicalNot, BitNot,
    CompareEqual, CompareNotEqual, CompareLess, CompareGreater,
    CompareLessEqual, CompareGreaterEqual, Convert,
    GetArgument, SetArgument, Call, CallIndirect, GetReturn, SetReturn,
    Label, Jump, Branch, Trap, Return
};

struct Instruction {
    Opcode opcode = Opcode::Trap;
    std::vector<Operand> operands;
    ast::SourceLocation loc;
};

struct Function {
    std::string name;
    hir::Type result = hir::Type::void_type();
    std::vector<hir::Type> parameter_types;
    bool variadic = false;
    bool external = false;
    std::size_t frame_size = 0;
    std::vector<ObjectPlacement> objects; // same order and indexes as hir::Function::objects
    std::vector<Instruction> instructions;
};

struct Global {
    std::string name;
    hir::Type type;
    std::optional<hir::Constant> initializer;
};
struct TranslationUnit {
    std::vector<Global> globals;
    std::vector<Function> functions;
};

class LoweringError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::size_t type_size(const hir::Type& type);
[[nodiscard]] std::size_t type_alignment(const hir::Type& type);
[[nodiscard]] std::size_t field_offset(const hir::Type& structure, std::size_t field_index);
[[nodiscard]] const char* opcode_name(Opcode opcode);
[[nodiscard]] TranslationUnit lower(const hir::TranslationUnit& unit);
void dump(const TranslationUnit& unit, std::ostream& out);

} // namespace lir
