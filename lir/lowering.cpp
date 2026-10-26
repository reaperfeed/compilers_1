#include "lowering.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <variant>

namespace lir {
namespace {
Register bp() { return {RegisterKind::BasePointer, 0}; }
Register sp() { return {RegisterKind::StackPointer, 0}; }

std::int64_t as_i64(std::size_t value, const std::string& what) {
    if (value > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
        throw LoweringError(what + " exceeds signed address range");
    return static_cast<std::int64_t>(value);
}

std::size_t align_value(std::size_t value, std::size_t alignment) {
    if (!alignment) throw LoweringError("cannot align a stack object to zero");
    const auto remainder = value % alignment;
    if (!remainder) return value;
    const auto add = alignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - add)
        throw LoweringError("stack frame size overflows addressable size");
    return value + add;
}

bool aggregate(const hir::Type& type) {
    return type.kind == hir::TypeKind::Array || type.kind == hir::TypeKind::Struct;
}

std::optional<hir::ObjectId> object_arg(const hir::Operation& operation, std::size_t index) {
    if (const auto* id = std::get_if<hir::ObjectId>(&operation.args[index])) return *id;
    return {};
}

class FunctionLowerer {
    const hir::TranslationUnit& unit_;
    const hir::Function& source_;
    lir::Function result_;
    std::vector<bool> address_taken_;
    std::size_t next_vreg_ = 0;

    Register fresh_register() { return {RegisterKind::Virtual, next_vreg_++}; }
    void emit(Opcode opcode, std::initializer_list<Operand> operands,
              ast::SourceLocation location = {}) {
        result_.instructions.push_back(Instruction{opcode, std::vector<Operand>(operands), location});
    }
    const hir::Object& object(hir::ObjectId id) const { return unit_.object(source_, id); }
    const hir::Type& type(hir::ObjectId id) const { return object(id).type; }

    const ObjectLocation& location(hir::ObjectId id) const {
        if (id.global || id.index >= result_.objects.size())
            throw LoweringError(source_.name + ": invalid local object location");
        return result_.objects[id.index].location;
    }

    Memory frame_memory(const FrameSlot& slot, std::size_t width) const {
        return Memory{bp(), slot.offset, width};
    }

    Symbol global_symbol(hir::ObjectId id) const {
        if (!id.global || id.index >= unit_.globals.size())
            throw LoweringError(source_.name + ": invalid global object ID");
        return Symbol{unit_.globals[id.index].name, false};
    }

    Register address_of(hir::ObjectId id, ast::SourceLocation loc) {
        auto address = fresh_register();
        if (id.global) {
            emit(Opcode::LoadAddress, {address, global_symbol(id)}, loc);
            return address;
        }
        const auto& where = location(id);
        const auto* slot = std::get_if<FrameSlot>(&where);
        if (!slot)
            throw LoweringError(source_.name + ": address-taken object was assigned to a virtual register");
        emit(Opcode::LoadAddress, {address, Memory{bp(), slot->offset, 0}}, loc);
        return address;
    }

    Register read_value(hir::ObjectId id, ast::SourceLocation loc) {
        const auto& object_type = type(id);
        if (aggregate(object_type))
            throw LoweringError(source_.name + ": aggregate used as a scalar value");
        if (id.global) {
            auto address = address_of(id, loc);
            auto value = fresh_register();
            emit(Opcode::Load, {value, Memory{address, 0, type_size(object_type)}}, loc);
            return value;
        }
        const auto& where = location(id);
        if (const auto* reg = std::get_if<Register>(&where)) return *reg;
        auto value = fresh_register();
        emit(Opcode::Load, {value, frame_memory(std::get<FrameSlot>(where), type_size(object_type))}, loc);
        return value;
    }

    void write_value(hir::ObjectId id, Operand value, ast::SourceLocation loc) {
        const auto& object_type = type(id);
        if (aggregate(object_type))
            throw LoweringError(source_.name + ": scalar write used for an aggregate object");
        if (id.global) {
            auto address = address_of(id, loc);
            auto reg = ensure_register(std::move(value), loc);
            emit(Opcode::Store, {Memory{address, 0, type_size(object_type)}, reg}, loc);
            return;
        }
        const auto& where = location(id);
        if (const auto* reg = std::get_if<Register>(&where)) {
            if (const auto* source_reg = std::get_if<Register>(&value)) {
                if (*reg != *source_reg) emit(Opcode::Move, {*reg, *source_reg}, loc);
            } else if (const auto* text = std::get_if<std::string>(&value)) {
                emit(Opcode::LoadAddress, {*reg, *text}, loc);
            } else {
                emit(Opcode::LoadImmediate, {*reg, std::move(value)}, loc);
            }
            return;
        }
        auto reg = ensure_register(std::move(value), loc);
        emit(Opcode::Store, {frame_memory(std::get<FrameSlot>(where), type_size(object_type)), reg}, loc);
    }

    Register ensure_register(Operand value, ast::SourceLocation loc) {
        if (const auto* reg = std::get_if<Register>(&value)) return *reg;
        auto temporary = fresh_register();
        if (const auto* text = std::get_if<std::string>(&value))
            emit(Opcode::LoadAddress, {temporary, *text}, loc);
        else
            emit(Opcode::LoadImmediate, {temporary, std::move(value)}, loc);
        return temporary;
    }

    Memory object_memory(hir::ObjectId id, ast::SourceLocation loc) {
        if (id.global) {
            auto address = address_of(id, loc);
            return Memory{address, 0, type_size(type(id))};
        }
        const auto* slot = std::get_if<FrameSlot>(&location(id));
        if (!slot) throw LoweringError(source_.name + ": aggregate object is not in memory");
        return frame_memory(*slot, type_size(type(id)));
    }

    void copy_aggregate(Memory destination, Memory source, const hir::Type& type,
                        ast::SourceLocation loc) {
        if (type.kind == hir::TypeKind::Array) {
            for (std::size_t i = 0; i < type.extent; ++i) {
                const auto step = type_size(*type.element);
                const auto delta = as_i64(i * step, "array element offset");
                copy_aggregate(Memory{destination.base, destination.displacement + delta, step},
                               Memory{source.base, source.displacement + delta, step}, *type.element, loc);
            }
            return;
        }
        if (type.kind == hir::TypeKind::Struct) {
            for (std::size_t i = 0; i < type.fields.size(); ++i) {
                const auto delta = as_i64(field_offset(type, i), "structure field offset");
                const auto width = type_size(type.fields[i].second);
                copy_aggregate(Memory{destination.base, destination.displacement + delta, width},
                               Memory{source.base, source.displacement + delta, width},
                               type.fields[i].second, loc);
            }
            return;
        }
        auto scratch = fresh_register();
        const auto width = type_size(type);
        emit(Opcode::Load, {scratch, Memory{source.base, source.displacement, width}}, loc);
        emit(Opcode::Store, {Memory{destination.base, destination.displacement, width}, scratch}, loc);
    }

    void zero_object(Memory destination, const hir::Type& type, ast::SourceLocation loc) {
        if (type.kind == hir::TypeKind::Array) {
            for (std::size_t i = 0; i < type.extent; ++i) {
                const auto step = type_size(*type.element);
                zero_object(Memory{destination.base,
                                   destination.displacement + as_i64(i * step, "array element offset"), step},
                            *type.element, loc);
            }
            return;
        }
        if (type.kind == hir::TypeKind::Struct) {
            for (std::size_t i = 0; i < type.fields.size(); ++i) {
                const auto delta = as_i64(field_offset(type, i), "structure field offset");
                const auto width = type_size(type.fields[i].second);
                zero_object(Memory{destination.base, destination.displacement + delta, width},
                            type.fields[i].second, loc);
            }
            return;
        }
        auto zero = fresh_register();
        emit(Opcode::LoadImmediate, {zero, int64_t{0}}, loc);
        emit(Opcode::Store, {Memory{destination.base, destination.displacement, type_size(type)}, zero}, loc);
    }

    void prepare_locations() {
        address_taken_.assign(source_.objects.size(), false);
        for (const auto& operation : source_.operations) {
            if (operation.opcode == hir::Opcode::AddressOf || operation.opcode == hir::Opcode::FieldAddress) {
                if (auto id = object_arg(operation, 1); id && !id->global && id->index < address_taken_.size())
                    address_taken_[id->index] = true;
            } else if (operation.opcode == hir::Opcode::ElementAddress) {
                if (auto id = object_arg(operation, 1); id && !id->global && id->index < address_taken_.size() &&
                    type(*id).kind == hir::TypeKind::Array)
                    address_taken_[id->index] = true;
            }
        }

        result_.objects.resize(source_.objects.size());
        std::size_t used = 0;
        for (std::size_t i = 0; i < source_.objects.size(); ++i) {
            const auto& object = source_.objects[i];
            const bool needs_memory = aggregate(object.type) || address_taken_[i];
            if (!needs_memory) {
                result_.objects[i] = ObjectPlacement{hir::ObjectId{i, false}, fresh_register()};
                continue;
            }
            const auto alignment = type_alignment(object.type);
            used = align_value(used, alignment);
            auto size = type_size(object.type);
            // Zero-length arrays still receive a distinct, addressable byte.
            const auto allocation_size = std::max<std::size_t>(size, 1);
            if (used > std::numeric_limits<std::size_t>::max() - allocation_size)
                throw LoweringError(source_.name + ": stack frame size overflow");
            used += allocation_size;
            const auto offset = -as_i64(used, "stack frame offset");
            result_.objects[i] = ObjectPlacement{hir::ObjectId{i, false}, FrameSlot{offset, size, alignment}};
        }
        result_.frame_size = align_value(used, 16);
    }

    void emit_prologue() {
        emit(Opcode::Push, {bp()});
        emit(Opcode::Move, {bp(), sp()});
        if (result_.frame_size) emit(Opcode::Sub, {sp(), sp(), static_cast<std::int64_t>(result_.frame_size)});
        for (std::size_t i = 0; i < source_.parameters.size(); ++i) {
            auto id = source_.parameters[i];
            const auto& where = location(id);
            if (const auto* reg = std::get_if<Register>(&where)) {
                emit(Opcode::GetArgument, {*reg, static_cast<std::int64_t>(i)});
            } else {
                emit(Opcode::GetArgument,
                     {frame_memory(std::get<FrameSlot>(where), type_size(type(id))), static_cast<std::int64_t>(i)});
            }
        }
    }

    void emit_epilogue(ast::SourceLocation loc) {
        emit(Opcode::Move, {sp(), bp()}, loc);
        emit(Opcode::Pop, {bp()}, loc);
        emit(Opcode::Return, {}, loc);
    }

    Register destination_register(hir::ObjectId id, ast::SourceLocation loc) {
        if (id.global) return fresh_register();
        if (const auto* reg = std::get_if<Register>(&location(id))) return *reg;
        (void)loc;
        return fresh_register();
    }

    void save_result(hir::ObjectId id, Register value, ast::SourceLocation loc) {
        if (id.global) {
            write_value(id, value, loc);
            return;
        }
        const auto& where = location(id);
        if (const auto* reg = std::get_if<Register>(&where)) {
            if (*reg != value) emit(Opcode::Move, {*reg, value}, loc);
        } else {
            emit(Opcode::Store,
                 {frame_memory(std::get<FrameSlot>(where), type_size(type(id))), value}, loc);
        }
    }

    Opcode lower_binary(hir::Opcode opcode) const {
        switch (opcode) {
        case hir::Opcode::Add: return Opcode::Add;
        case hir::Opcode::Sub: return Opcode::Sub;
        case hir::Opcode::Mul: return Opcode::Mul;
        case hir::Opcode::Div: return Opcode::Div;
        case hir::Opcode::Mod: return Opcode::Mod;
        case hir::Opcode::BitAnd: return Opcode::BitAnd;
        case hir::Opcode::BitOr: return Opcode::BitOr;
        case hir::Opcode::BitXor: return Opcode::BitXor;
        case hir::Opcode::Shl: return Opcode::ShiftLeft;
        case hir::Opcode::Shr: return Opcode::ShiftRight;
        case hir::Opcode::Eq: return Opcode::CompareEqual;
        case hir::Opcode::Ne: return Opcode::CompareNotEqual;
        case hir::Opcode::Lt: return Opcode::CompareLess;
        case hir::Opcode::Gt: return Opcode::CompareGreater;
        case hir::Opcode::Le: return Opcode::CompareLessEqual;
        case hir::Opcode::Ge: return Opcode::CompareGreaterEqual;
        default: throw LoweringError("unsupported binary HIR operation");
        }
    }

    Operand constant_operand(const hir::Constant& constant) {
        return std::visit([&](const auto& value) -> Operand {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                switch (constant.type.kind) {
                case hir::TypeKind::Float: return 0.0;
                case hir::TypeKind::Bool: return false;
                case hir::TypeKind::Char: return char{0};
                default: return int64_t{0};
                }
            }
            else return value;
        }, constant.value);
    }

    void lower_constant(const hir::Operation& operation) {
        const auto destination = object_arg(operation, 0);
        if (!destination) throw LoweringError("constant has no destination object");
        const auto* constant = std::get_if<hir::Constant>(&operation.args[1]);
        if (!constant) throw LoweringError("constant instruction has no constant operand");
        const auto& destination_type = type(*destination);
        if (aggregate(destination_type)) {
            if (!std::holds_alternative<std::monostate>(constant->value))
                throw LoweringError("only zero aggregate initialization is supported");
            zero_object(object_memory(*destination, operation.loc), destination_type, operation.loc);
            return;
        }
        auto reg = fresh_register();
        if (const auto* string = std::get_if<std::string>(&constant->value))
            emit(Opcode::LoadAddress, {reg, *string}, operation.loc);
        else
            emit(Opcode::LoadImmediate, {reg, constant_operand(*constant)}, operation.loc);
        save_result(*destination, reg, operation.loc);
    }

    void lower_copy(const hir::Operation& operation) {
        auto destination = object_arg(operation, 0), source = object_arg(operation, 1);
        if (!destination || !source) throw LoweringError("copy requires two object operands");
        const auto& t = type(*destination);
        if (aggregate(t)) {
            copy_aggregate(object_memory(*destination, operation.loc),
                           object_memory(*source, operation.loc), t, operation.loc);
            return;
        }
        save_result(*destination, read_value(*source, operation.loc), operation.loc);
    }

    void lower_address(const hir::Operation& operation) {
        auto destination = object_arg(operation, 0), source = object_arg(operation, 1);
        if (!destination || !source) throw LoweringError("address-of requires object operands");
        auto address = address_of(*source, operation.loc);
        save_result(*destination, address, operation.loc);
    }

    void lower_element_address(const hir::Operation& operation) {
        auto destination = object_arg(operation, 0), base = object_arg(operation, 1), index = object_arg(operation, 2);
        if (!destination || !base || !index) throw LoweringError("element-address requires three object operands");
        const auto& base_type = type(*base);
        if (!base_type.element) throw LoweringError("element-address base has no element type");
        auto address = base_type.kind == hir::TypeKind::Pointer
            ? read_value(*base, operation.loc)
            : address_of(*base, operation.loc);
        auto index_value = read_value(*index, operation.loc);
        const auto element_size = type_size(*base_type.element);
        Register displacement = index_value;
        if (element_size != 1) {
            displacement = fresh_register();
            emit(Opcode::Mul, {displacement, index_value, static_cast<std::int64_t>(element_size)}, operation.loc);
        }
        auto result = destination_register(*destination, operation.loc);
        emit(Opcode::Add, {result, address, displacement}, operation.loc);
        save_result(*destination, result, operation.loc);
    }

    void lower_field_address(const hir::Operation& operation) {
        auto destination = object_arg(operation, 0), base = object_arg(operation, 1);
        const auto* field = std::get_if<hir::FieldIndex>(&operation.args[2]);
        if (!destination || !base || !field) throw LoweringError("field-address operands are malformed");
        auto address = address_of(*base, operation.loc);
        const auto offset = field_offset(type(*base), field->index);
        auto result = destination_register(*destination, operation.loc);
        if (offset) emit(Opcode::Add, {result, address, static_cast<std::int64_t>(offset)}, operation.loc);
        else emit(Opcode::Move, {result, address}, operation.loc);
        save_result(*destination, result, operation.loc);
    }

    void lower_load(const hir::Operation& operation) {
        auto destination = object_arg(operation, 0), pointer = object_arg(operation, 1);
        if (!destination || !pointer) throw LoweringError("load operands are malformed");
        auto address = read_value(*pointer, operation.loc);
        if (aggregate(type(*destination))) {
            copy_aggregate(object_memory(*destination, operation.loc),
                           Memory{address, 0, type_size(type(*destination))},
                           type(*destination), operation.loc);
            return;
        }
        auto result = destination_register(*destination, operation.loc);
        emit(Opcode::Load, {result, Memory{address, 0, type_size(type(*destination))}}, operation.loc);
        save_result(*destination, result, operation.loc);
    }

    void lower_store(const hir::Operation& operation) {
        auto pointer = object_arg(operation, 0), value = object_arg(operation, 1);
        if (!pointer || !value) throw LoweringError("store operands are malformed");
        auto address = read_value(*pointer, operation.loc);
        if (aggregate(type(*value))) {
            copy_aggregate(Memory{address, 0, type_size(type(*value))},
                           object_memory(*value, operation.loc), type(*value), operation.loc);
            return;
        }
        auto source = read_value(*value, operation.loc);
        emit(Opcode::Store, {Memory{address, 0, type_size(type(*value))}, source}, operation.loc);
    }

    void lower_call(const hir::Operation& operation, bool indirect) {
        for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
            auto id = operation.arguments[i];
            if (aggregate(type(id))) {
                auto memory = object_memory(id, operation.loc);
                emit(Opcode::SetArgument, {static_cast<std::int64_t>(i), memory}, operation.loc);
            } else {
                emit(Opcode::SetArgument, {static_cast<std::int64_t>(i), read_value(id, operation.loc)}, operation.loc);
            }
        }
        if (indirect) {
            auto callee = object_arg(operation, 1);
            if (!callee) throw LoweringError("indirect call has no function-pointer operand");
            emit(Opcode::CallIndirect, {read_value(*callee, operation.loc)}, operation.loc);
        } else {
            const auto* id = std::get_if<hir::FunctionId>(&operation.args[1]);
            if (!id || id->index >= unit_.functions.size()) throw LoweringError("call has invalid function ID");
            emit(Opcode::Call, {Symbol{unit_.functions[id->index].name, true}}, operation.loc);
        }
        if (auto destination = object_arg(operation, 0)) {
            if (aggregate(type(*destination))) {
                emit(Opcode::GetReturn, {object_memory(*destination, operation.loc)}, operation.loc);
            } else {
                auto result = destination_register(*destination, operation.loc);
                emit(Opcode::GetReturn, {result}, operation.loc);
                save_result(*destination, result, operation.loc);
            }
        }
    }

    void lower_return(const hir::Operation& operation) {
        if (auto value = object_arg(operation, 0)) {
            if (aggregate(type(*value))) emit(Opcode::SetReturn, {object_memory(*value, operation.loc)}, operation.loc);
            else emit(Opcode::SetReturn, {read_value(*value, operation.loc)}, operation.loc);
        }
        emit_epilogue(operation.loc);
    }

    void lower_operation(const hir::Operation& operation) {
        switch (operation.opcode) {
        case hir::Opcode::Constant: lower_constant(operation); return;
        case hir::Opcode::Copy: lower_copy(operation); return;
        case hir::Opcode::AddressOf: lower_address(operation); return;
        case hir::Opcode::ElementAddress: lower_element_address(operation); return;
        case hir::Opcode::FieldAddress: lower_field_address(operation); return;
        case hir::Opcode::Load: lower_load(operation); return;
        case hir::Opcode::Store: lower_store(operation); return;
        case hir::Opcode::Label:
            emit(Opcode::Label, {std::get<hir::LabelId>(operation.args[0])}, operation.loc); return;
        case hir::Opcode::Jump:
            emit(Opcode::Jump, {std::get<hir::LabelId>(operation.args[0])}, operation.loc); return;
        case hir::Opcode::Branch:
            emit(Opcode::Branch, {read_value(*object_arg(operation, 0), operation.loc),
                                  std::get<hir::LabelId>(operation.args[1]),
                                  std::get<hir::LabelId>(operation.args[2])}, operation.loc); return;
        case hir::Opcode::Call: lower_call(operation, false); return;
        case hir::Opcode::CallIndirect: lower_call(operation, true); return;
        case hir::Opcode::Return: lower_return(operation); return;
        case hir::Opcode::Unreachable:
            emit(Opcode::Trap, {}, operation.loc); return;
        case hir::Opcode::Convert: {
            auto destination = object_arg(operation, 0), source = object_arg(operation, 1);
            if (!destination || !source) throw LoweringError("convert operands are malformed");
            auto result = destination_register(*destination, operation.loc);
            emit(Opcode::Convert, {result, read_value(*source, operation.loc), type(*destination).str()}, operation.loc);
            save_result(*destination, result, operation.loc); return;
        }
        case hir::Opcode::Neg: case hir::Opcode::LogicalNot: case hir::Opcode::BitNot: {
            auto destination = object_arg(operation, 0), source = object_arg(operation, 1);
            if (!destination || !source) throw LoweringError("unary operation operands are malformed");
            auto result = destination_register(*destination, operation.loc);
            const auto opcode = operation.opcode == hir::Opcode::Neg ? Opcode::Neg :
                                operation.opcode == hir::Opcode::LogicalNot ? Opcode::LogicalNot : Opcode::BitNot;
            emit(opcode, {result, read_value(*source, operation.loc)}, operation.loc);
            save_result(*destination, result, operation.loc); return;
        }
        default: break;
        }

        const auto destination = object_arg(operation, 0);
        const auto left = object_arg(operation, 1);
        const auto right = object_arg(operation, 2);
        if (!destination || !left || !right) throw LoweringError("binary operation operands are malformed");
        auto result = destination_register(*destination, operation.loc);
        emit(lower_binary(operation.opcode),
             {result, read_value(*left, operation.loc), read_value(*right, operation.loc)}, operation.loc);
        save_result(*destination, result, operation.loc);
    }

public:
    FunctionLowerer(const hir::TranslationUnit& unit, const hir::Function& function)
        : unit_(unit), source_(function) {
        result_.name = function.name;
        result_.result = function.result;
        result_.parameter_types = function.parameter_types;
        result_.variadic = function.variadic;
        result_.external = function.external;
    }

    lir::Function run() {
        if (source_.external) return std::move(result_);
        prepare_locations();
        emit_prologue();
        for (const auto& operation : source_.operations) lower_operation(operation);
        return std::move(result_);
    }
};

} // namespace

TranslationUnit lower(const hir::TranslationUnit& unit) {
    hir::verify(unit);
    TranslationUnit result;
    result.globals.reserve(unit.globals.size());
    for (const auto& global : unit.globals)
        result.globals.push_back(Global{global.name, global.type, global.initializer});
    result.functions.reserve(unit.functions.size());
    for (const auto& function : unit.functions)
        result.functions.push_back(FunctionLowerer(unit, function).run());
    return result;
}

} // namespace lir
