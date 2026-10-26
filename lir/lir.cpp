#include "lir.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace lir {
namespace {
constexpr std::size_t kWordSize = 8;

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (!alignment) throw LoweringError("zero type alignment");
    const auto remainder = value % alignment;
    if (!remainder) return value;
    const auto add = alignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - add)
        throw LoweringError("type layout overflows addressable size");
    return value + add;
}

std::string register_text(Register r) {
    switch (r.kind) {
    case RegisterKind::Virtual: return "%v" + std::to_string(r.index);
    case RegisterKind::StackPointer: return "sp";
    case RegisterKind::BasePointer: return "bp";
    }
    return "?reg";
}

std::string width_text(std::size_t width) {
    switch (width) {
    case 1: return "byte ";
    case 2: return "word ";
    case 4: return "dword ";
    case 8: return "qword ";
    default: return width ? std::to_string(width) + "-byte " : "";
    }
}

void quoted(std::ostream& out, const std::string& value) {
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
}

void print_operand(std::ostream& out, const Operand& operand) {
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) out << "<none>";
        else if constexpr (std::is_same_v<T, Register>) out << register_text(value);
        else if constexpr (std::is_same_v<T, Memory>) {
            out << width_text(value.width) << '[' << register_text(value.base);
            if (value.displacement > 0) out << " + " << value.displacement;
            else if (value.displacement < 0) out << " - " << -value.displacement;
            out << ']';
        } else if constexpr (std::is_same_v<T, std::int64_t>) out << value;
        else if constexpr (std::is_same_v<T, double>) out << std::setprecision(17) << value;
        else if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
        else if constexpr (std::is_same_v<T, char>) out << "'" << value << "'";
        else if constexpr (std::is_same_v<T, std::string>) quoted(out, value);
        else if constexpr (std::is_same_v<T, hir::LabelId>) out << 'L' << value.index;
        else if constexpr (std::is_same_v<T, Symbol>) out << (value.function ? "@f:" : "@g:") << value.name;
    }, operand);
}

void print_constant(std::ostream& out, const hir::Constant& constant) {
    out << constant.type.str() << ' ';
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) out << "zero";
        else if constexpr (std::is_same_v<T, std::string>) quoted(out, value);
        else if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
        else if constexpr (std::is_same_v<T, char>) out << static_cast<int>(static_cast<unsigned char>(value));
        else if constexpr (std::is_same_v<T, double>) out << std::setprecision(17) << value;
        else out << value;
    }, constant.value);
}

void print_location(std::ostream& out, const ObjectLocation& location) {
    if (const auto* reg = std::get_if<Register>(&location)) {
        out << register_text(*reg);
        return;
    }
    const auto& slot = std::get<FrameSlot>(location);
    out << '[' << "bp";
    if (slot.offset > 0) out << " + " << slot.offset;
    else if (slot.offset < 0) out << " - " << -slot.offset;
    out << "] (" << slot.size << " bytes, align " << slot.alignment << ')';
}
}

std::size_t type_size(const hir::Type& type) {
    switch (type.kind) {
    case hir::TypeKind::Int: case hir::TypeKind::Float: case hir::TypeKind::Pointer:
        return kWordSize;
    case hir::TypeKind::Bool: case hir::TypeKind::Char:
        return 1;
    case hir::TypeKind::Array: {
        if (!type.element) throw LoweringError("array type has no element type");
        const auto element = type_size(*type.element);
        if (type.extent && element > std::numeric_limits<std::size_t>::max() / type.extent)
            throw LoweringError("array size overflows addressable size");
        return element * type.extent;
    }
    case hir::TypeKind::Struct: {
        std::size_t cursor = 0;
        std::size_t alignment = 1;
        for (const auto& field : type.fields) {
            const auto field_alignment = type_alignment(field.second);
            alignment = std::max(alignment, field_alignment);
            cursor = align_up(cursor, field_alignment);
            const auto size = type_size(field.second);
            if (cursor > std::numeric_limits<std::size_t>::max() - size)
                throw LoweringError("structure size overflows addressable size");
            cursor += size;
        }
        return align_up(cursor, alignment);
    }
    case hir::TypeKind::Void: case hir::TypeKind::Function:
        throw LoweringError("void/function type has no object storage size");
    }
    throw LoweringError("unknown type kind");
}

std::size_t type_alignment(const hir::Type& type) {
    switch (type.kind) {
    case hir::TypeKind::Int: case hir::TypeKind::Float: case hir::TypeKind::Pointer:
        return kWordSize;
    case hir::TypeKind::Bool: case hir::TypeKind::Char:
        return 1;
    case hir::TypeKind::Array:
        if (!type.element) throw LoweringError("array type has no element type");
        return type_alignment(*type.element);
    case hir::TypeKind::Struct: {
        std::size_t alignment = 1;
        for (const auto& field : type.fields) alignment = std::max(alignment, type_alignment(field.second));
        return alignment;
    }
    case hir::TypeKind::Void: case hir::TypeKind::Function:
        throw LoweringError("void/function type has no object alignment");
    }
    throw LoweringError("unknown type kind");
}

std::size_t field_offset(const hir::Type& structure, std::size_t field_index) {
    if (structure.kind != hir::TypeKind::Struct || field_index >= structure.fields.size())
        throw LoweringError("field index is invalid for structure type");
    std::size_t cursor = 0;
    for (std::size_t i = 0; i <= field_index; ++i) {
        const auto alignment = type_alignment(structure.fields[i].second);
        cursor = align_up(cursor, alignment);
        if (i == field_index) return cursor;
        const auto size = type_size(structure.fields[i].second);
        if (cursor > std::numeric_limits<std::size_t>::max() - size)
            throw LoweringError("structure field offset overflows addressable size");
        cursor += size;
    }
    throw LoweringError("field index is invalid for structure type");
}

const char* opcode_name(Opcode opcode) {
    switch (opcode) {
#define LIR_NAME(code, text) case Opcode::code: return text
    LIR_NAME(Push, "push"); LIR_NAME(Pop, "pop"); LIR_NAME(Move, "mov");
    LIR_NAME(LoadImmediate, "li"); LIR_NAME(LoadAddress, "lea");
    LIR_NAME(Load, "ld"); LIR_NAME(Store, "st");
    LIR_NAME(Add, "add"); LIR_NAME(Sub, "sub"); LIR_NAME(Mul, "mul");
    LIR_NAME(Div, "div"); LIR_NAME(Mod, "mod"); LIR_NAME(BitAnd, "and");
    LIR_NAME(BitOr, "or"); LIR_NAME(BitXor, "xor"); LIR_NAME(ShiftLeft, "shl");
    LIR_NAME(ShiftRight, "shr"); LIR_NAME(Neg, "neg");
    LIR_NAME(LogicalNot, "not"); LIR_NAME(BitNot, "bitnot");
    LIR_NAME(CompareEqual, "cmp_eq"); LIR_NAME(CompareNotEqual, "cmp_ne");
    LIR_NAME(CompareLess, "cmp_lt"); LIR_NAME(CompareGreater, "cmp_gt");
    LIR_NAME(CompareLessEqual, "cmp_le"); LIR_NAME(CompareGreaterEqual, "cmp_ge");
    LIR_NAME(Convert, "convert"); LIR_NAME(GetArgument, "getarg");
    LIR_NAME(SetArgument, "setarg"); LIR_NAME(Call, "call");
    LIR_NAME(CallIndirect, "call_indirect"); LIR_NAME(GetReturn, "getret");
    LIR_NAME(SetReturn, "setret"); LIR_NAME(Label, "label");
    LIR_NAME(Jump, "jump"); LIR_NAME(Branch, "branch");
    LIR_NAME(Trap, "trap"); LIR_NAME(Return, "ret");
#undef LIR_NAME
    }
    return "invalid";
}

void dump(const TranslationUnit& unit, std::ostream& out) {
    out << "; LIR: 8-byte word, byte-addressed stack, locals addressed from bp\n";
    for (const auto& global : unit.globals) {
        out << "global @g:" << global.name << ": " << global.type.str();
        if (global.initializer) {
            out << " = ";
            print_constant(out, *global.initializer);
        }
        out << '\n';
    }
    for (const auto& function : unit.functions) {
        out << '\n' << (function.external ? "extern " : "function ") << '@'
            << "f:" << function.name << " fn(";
        for (std::size_t i = 0; i < function.parameter_types.size(); ++i) {
            if (i) out << ", ";
            out << function.parameter_types[i].str();
        }
        if (function.variadic) out << (function.parameter_types.empty() ? "..." : ", ...");
        out << ") -> " << function.result.str() << '\n';
        if (function.external) continue;
        out << "  frame " << function.frame_size << " bytes\n";
        for (std::size_t i = 0; i < function.objects.size(); ++i) {
            const auto& placement = function.objects[i];
            out << "  object %" << i << " -> ";
            print_location(out, placement.location);
            out << '\n';
        }
        for (const auto& instruction : function.instructions) {
            out << "  " << opcode_name(instruction.opcode);
            for (std::size_t i = 0; i < instruction.operands.size(); ++i) {
                out << (i == 0 ? " " : ", ");
                print_operand(out, instruction.operands[i]);
            }
            out << '\n';
        }
    }
}

} // namespace lir
