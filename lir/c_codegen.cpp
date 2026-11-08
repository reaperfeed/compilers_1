#include "c_codegen.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <type_traits>

namespace lir {
namespace {
using Kind = hir::TypeKind;
bool aggregate(const hir::Type& t) { return t.kind == Kind::Array || t.kind == Kind::Struct; }
bool reg_is(const Operand& operand, RegisterKind kind) {
    auto r = std::get_if<Register>(&operand);
    return r && r->kind == kind;
}
std::size_t aligned(std::size_t n) {
    if (n > static_cast<std::size_t>(INT64_MAX) - 15) throw LoweringError("C codegen: frame too large");
    return (n + 15) / 16 * 16;
}
const char* tag(const hir::Type& t) {
    switch (t.kind) {
    case Kind::Void: return "VM_T_VOID";
    case Kind::Int: return "VM_T_INT";
    case Kind::Float: return "VM_T_FLOAT";
    case Kind::Bool: return "VM_T_BOOL";
    case Kind::Char: return "VM_T_CHAR";
    case Kind::Pointer:
        return t.element && t.element->kind == Kind::Char ? "VM_T_STRING" : "VM_T_POINTER";
    default: return "VM_T_AGGREGATE";
    }
}

class CEmitter {
    const TranslationUnit& unit_;
    std::ostringstream body_;
    std::map<uint64_t, std::size_t> words_;
    std::map<std::string, std::size_t> strings_;
    std::map<std::string, std::string> functions_;
    std::map<std::string, std::string> globals_;
    const Function* function_ = nullptr;
    std::size_t registers_ = 0;
    std::size_t pending_args_ = 0;

    void line(const std::string& s) { body_ << "    " << s << '\n'; }
    void macro(const std::string& name, const std::string& args = "") { line(name + "(" + args + ");"); }
    std::string word(uint64_t bits) {
        auto [it, inserted] = words_.emplace(bits, words_.size());
        (void)inserted;
        return "vm_constant_" + std::to_string(it->second);
    }
    std::string string(const std::string& value) {
        auto [it, inserted] = strings_.emplace(value, strings_.size());
        (void)inserted;
        return "vm_string_" + std::to_string(it->second);
    }
    void constant(int dest, uint64_t bits) {
        // r[11] is exclusively the address scratch for global literals.
        macro("VM_ADDR", "11, " + word(bits));
        macro("VM_LD64", std::to_string(dest) + ", 11");
    }
    static uint64_t bits(const Operand& operand) {
        return std::visit([](const auto& x) -> uint64_t {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, double>) return std::bit_cast<uint64_t>(x);
            else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, bool> || std::is_same_v<T, char>)
                return static_cast<uint64_t>(x);
            else throw LoweringError("C codegen: expected a scalar literal");
        }, operand);
    }
    std::string symbol(const Symbol& s) const {
        const auto& table = s.function ? functions_ : globals_;
        auto it = table.find(s.name);
        if (it == table.end()) throw LoweringError("C codegen: unsupported symbol " + s.name);
        return it->second;
    }
    int physical(Register r) const {
        if (r.kind == RegisterKind::StackPointer) return 14;
        if (r.kind == RegisterKind::BasePointer) return 15;
        throw LoweringError("C codegen: virtual register has no permanent hardware register");
    }
    int64_t spill_offset(Register r) const {
        if (r.index >= registers_) throw LoweringError("C codegen: invalid virtual register");
        return -static_cast<int64_t>(function_->frame_size + (r.index + 1) * 8);
    }
    void slot_address(int target, int64_t offset) {
        constant(13, static_cast<uint64_t>(offset));
        macro("VM_ADD", std::to_string(target) + ", VM_BP, 13");
    }
    void read_register(Register r, int dest) {
        if (r.kind != RegisterKind::Virtual) {
            macro("VM_MOV", std::to_string(dest) + ", " + std::to_string(physical(r)));
        } else {
            slot_address(12, spill_offset(r));
            macro("VM_LD64", std::to_string(dest) + ", 12");
        }
    }
    void write_register(Register r, int source) {
        if (r.kind != RegisterKind::Virtual) {
            macro("VM_MOV", std::to_string(physical(r)) + ", " + std::to_string(source));
        } else {
            slot_address(12, spill_offset(r));
            macro("VM_ST64", "12, " + std::to_string(source));
        }
    }
    static std::string width(std::size_t n) {
        if (n != 1 && n != 2 && n != 4 && n != 8)
            throw LoweringError("C codegen: scalar access must be 1, 2, 4 or 8 bytes");
        return std::to_string(n * 8);
    }
    void address(const Memory& m, int dest) {
        read_register(m.base, dest);
        if (m.displacement) {
            constant(13, static_cast<uint64_t>(m.displacement));
            macro("VM_ADD", std::to_string(dest) + ", " + std::to_string(dest) + ", 13");
        }
    }
    void read(const Operand& operand, int dest) {
        if (auto r = std::get_if<Register>(&operand)) read_register(*r, dest);
        else if (auto m = std::get_if<Memory>(&operand)) {
            address(*m, 12);
            macro("VM_LD" + width(m->width), std::to_string(dest) + ", 12");
        } else constant(dest, bits(operand));
    }
    void write(const Operand& operand, int source) {
        if (auto r = std::get_if<Register>(&operand)) write_register(*r, source);
        else if (auto m = std::get_if<Memory>(&operand)) {
            address(*m, 12);
            macro("VM_ST" + width(m->width), "12, " + std::to_string(source));
        } else throw LoweringError("C codegen: invalid destination");
    }
    static std::size_t arg_index(const Operand& operand) {
        auto i = std::get_if<int64_t>(&operand);
        if (!i || *i < 0 || *i >= 256) throw LoweringError("C codegen: argument index out of range (0..255)");
        return static_cast<std::size_t>(*i);
    }
    static std::string label(const Operand& operand) {
        auto l = std::get_if<hir::LabelId>(&operand);
        if (!l) throw LoweringError("C codegen: expected a label");
        return "vm_label_" + std::to_string(l->index);
    }
    static bool epilogue(const Function& f, std::size_t i) {
        if (i + 2 >= f.instructions.size()) return false;
        const auto& a = f.instructions[i]; const auto& b = f.instructions[i+1];
        return a.opcode == Opcode::Move && a.operands.size() == 2 &&
            reg_is(a.operands[0], RegisterKind::StackPointer) && reg_is(a.operands[1], RegisterKind::BasePointer) &&
            b.opcode == Opcode::Pop && b.operands.size() == 1 && reg_is(b.operands[0], RegisterKind::BasePointer) &&
            f.instructions[i+2].opcode == Opcode::Return;
    }
    std::size_t layout(const Function& f) {
        registers_ = 0;
        auto visit = [&](Register r) {
            if (r.kind != RegisterKind::Virtual) return;
            if (r.index >= static_cast<std::size_t>(INT64_MAX) / 8 - 1)
                throw LoweringError("C codegen: too many virtual registers");
            registers_ = std::max(registers_, r.index + 1);
        };
        for (const auto& p : f.objects) if (auto r = std::get_if<Register>(&p.location)) visit(*r);
        for (const auto& ins : f.instructions) for (const auto& arg : ins.operands) {
            if (auto r = std::get_if<Register>(&arg)) visit(*r);
            if (auto m = std::get_if<Memory>(&arg)) visit(m->base);
        }
        if (f.frame_size > static_cast<std::size_t>(INT64_MAX) - registers_ * 8)
            throw LoweringError("C codegen: frame size overflow");
        auto size = aligned(f.frame_size + registers_ * 8);
        if (size > 16 * 1024 * 1024 - 8) throw LoweringError("C codegen: frame exceeds VM stack");
        return size;
    }
    static const char* binary(Opcode opcode, bool real) {
        switch (opcode) {
        case Opcode::Add: return real ? "VM_FADD" : "VM_ADD";
        case Opcode::Sub: return real ? "VM_FSUB" : "VM_SUB";
        case Opcode::Mul: return real ? "VM_FMUL" : "VM_MUL";
        case Opcode::Div: return real ? "VM_FDIV" : "VM_SDIV";
        case Opcode::Mod: return "VM_SMOD";
        case Opcode::BitAnd: return "VM_AND";
        case Opcode::BitOr: return "VM_OR";
        case Opcode::BitXor: return "VM_XOR";
        case Opcode::ShiftLeft: return "VM_SHL";
        case Opcode::ShiftRight: return "VM_SHR";
        case Opcode::CompareEqual: return real ? "VM_FEQ" : "VM_EQ";
        case Opcode::CompareNotEqual: return real ? "VM_FNE" : "VM_NE";
        case Opcode::CompareLess: return real ? "VM_FLT" : "VM_SLT";
        case Opcode::CompareLessEqual: return real ? "VM_FLE" : "VM_SLE";
        case Opcode::CompareGreater: return real ? "VM_FGT" : "VM_SGT";
        case Opcode::CompareGreaterEqual: return real ? "VM_FGE" : "VM_SGE";
        default: throw LoweringError("C codegen: unsupported binary instruction");
        }
    }
    void instruction(const Instruction& ins) {
        const auto& a = ins.operands;
        const bool block = aggregate(ins.value_type);
        switch (ins.opcode) {
        case Opcode::Move: case Opcode::LoadImmediate:
            read(a.at(1), 8); write(a.at(0), 8); return;
        case Opcode::LoadAddress:
            if (auto m = std::get_if<Memory>(&a.at(1))) address(*m, 8);
            else if (auto s = std::get_if<Symbol>(&a.at(1)))
                macro(s->function ? "VM_FADDR" : "VM_ADDR", "8, " + symbol(*s));
            else if (auto s = std::get_if<std::string>(&a.at(1))) macro("VM_ADDR", "8, " + string(*s));
            else throw LoweringError("C codegen: invalid address source");
            write(a.at(0), 8); return;
        case Opcode::Load: read(a.at(1), 8); write(a.at(0), 8); return;
        case Opcode::Store: read(a.at(1), 8); write(a.at(0), 8); return;
        case Opcode::Push: read(a.at(0), 8); macro("VM_PUSH", "8"); return;
        case Opcode::Pop: macro("VM_POP", "8"); write(a.at(0), 8); return;
        case Opcode::Label: line("VM_LABEL(" + label(a.at(0)) + ")"); return;
        case Opcode::Jump: macro("VM_JUMP", label(a.at(0))); return;
        case Opcode::Branch:
            read(a.at(0), 8); macro("VM_BRANCH", "8, " + label(a.at(1)) + ", " + label(a.at(2))); return;
        case Opcode::Trap: macro("VM_TRAP"); return;
        case Opcode::Return: throw LoweringError("C codegen: return without LIR epilogue");
        case Opcode::SetArgument: {
            auto index = arg_index(a.at(0));
            if (!index) { macro("VM_ARGS_BEGIN"); pending_args_ = 0; }
            if (index != pending_args_) throw LoweringError("C codegen: arguments must be prepared in order");
            if (block) address(std::get<Memory>(a.at(1)), 8);
            else read(a.at(1), 8);
            macro("VM_SETARG", std::to_string(index) + ", 8, " + tag(ins.value_type));
            ++pending_args_; return;
        }
        case Opcode::GetArgument: {
            auto index = arg_index(a.at(1));
            if (block) {
                address(std::get<Memory>(a.at(0)), 8); constant(9, type_size(ins.value_type));
                macro("VM_GETARG_BLOCK", "8, " + std::to_string(index) + ", 9");
            } else {
                macro("VM_GETARG", "8, " + std::to_string(index)); write(a.at(0), 8);
            }
            return;
        }
        case Opcode::Call:
            if (!pending_args_) macro("VM_ARGS_BEGIN");
            macro("VM_CALL", symbol(std::get<Symbol>(a.at(0)))); pending_args_ = 0; return;
        case Opcode::CallIndirect:
            if (!pending_args_) macro("VM_ARGS_BEGIN");
            read(a.at(0), 8); macro("VM_ICALL", "8"); pending_args_ = 0; return;
        case Opcode::GetReturn:
            if (block) {
                address(std::get<Memory>(a.at(0)), 8); constant(9, type_size(ins.value_type));
                macro("VM_GETRET_BLOCK", "8, 9");
            } else { macro("VM_GETRET", "8"); write(a.at(0), 8); }
            return;
        case Opcode::SetReturn:
            if (block) {
                address(std::get<Memory>(a.at(0)), 8); constant(9, type_size(ins.value_type));
                macro("VM_SETRET_BLOCK", "8, 9");
            } else { read(a.at(0), 8); macro("VM_SETRET", "8"); }
            return;
        case Opcode::Convert: {
            read(a.at(1), 8);
            const auto& target = std::get<std::string>(a.at(2));
            if (target == "f32" && ins.value_type.kind != Kind::Float) macro("VM_I2F", "10, 8");
            else if (target == "i32" && ins.value_type.kind == Kind::Float) macro("VM_F2I", "10, 8");
            else if (target == "bool") {
                if (ins.value_type.kind == Kind::Float) { constant(9, 0); macro("VM_FNE", "10, 8, 9"); }
                else macro("VM_BOOL", "10, 8");
            } else if (target == "char") macro("VM_CHAR", "10, 8");
            else if (target == "i32" || target == "f32") macro("VM_MOV", "10, 8");
            else throw LoweringError("C codegen: unsupported conversion to " + target);
            write(a.at(0), 10); return;
        }
        case Opcode::Neg: case Opcode::LogicalNot: case Opcode::BitNot:
            read(a.at(1), 8);
            macro(ins.opcode == Opcode::Neg ? (ins.value_type.kind == Kind::Float ? "VM_FNEG" : "VM_NEG") :
                  ins.opcode == Opcode::LogicalNot ? "VM_NOT" : "VM_BITNOT", "10, 8");
            write(a.at(0), 10); return;
        default:
            read(a.at(1), 8); read(a.at(2), 9);
            macro(binary(ins.opcode, ins.value_type.kind == Kind::Float), "10, 8, 9");
            write(a.at(0), 10); return;
        }
    }
    void function(const Function& f) {
        function_ = &f; pending_args_ = 0;
        auto frame = layout(f);
        const auto& v = f.instructions;
        if (v.size() < 2 || v[0].opcode != Opcode::Push || v[0].operands.size() != 1 ||
            !reg_is(v[0].operands[0], RegisterKind::BasePointer) ||
            v[1].opcode != Opcode::Move || v[1].operands.size() != 2 ||
            !reg_is(v[1].operands[0], RegisterKind::BasePointer) ||
            !reg_is(v[1].operands[1], RegisterKind::StackPointer))
            throw LoweringError("C codegen: function has no standard LIR prologue");
        std::size_t start = 2;
        if (f.frame_size) {
            if (v.size() < 3 || v[2].opcode != Opcode::Sub || v[2].operands.size() != 3 ||
                !reg_is(v[2].operands[0], RegisterKind::StackPointer) ||
                !reg_is(v[2].operands[1], RegisterKind::StackPointer) ||
                std::get<int64_t>(v[2].operands[2]) != static_cast<int64_t>(f.frame_size))
                throw LoweringError("C codegen: invalid LIR frame allocation");
            start = 3;
        }
        body_ << "static void " << functions_.at(f.name) << "(void) {\n";
        line("/* Local objects: " + std::to_string(f.frame_size) + "; spilled virtual registers: " +
             std::to_string(registers_) + "; total frame: " + std::to_string(frame) + ". */");
        constant(13, frame); macro("VM_ENTER", "13");
        for (std::size_t i = start; i < v.size(); ++i) {
            if (epilogue(f, i)) {
                if (f.result.kind == Kind::Void) macro("VM_ZERO", "0");
                macro("VM_LEAVE"); macro("VM_RET"); i += 2;
            } else instruction(v[i]);
        }
        if (pending_args_) throw LoweringError("C codegen: argument preparation without a call");
        body_ << "}\n\n";
    }
    void globals(std::ostream& out) {
        for (const auto& g : unit_.globals) {
            auto name = globals_.at(g.name);
            if (g.initializer && std::holds_alternative<std::string>(g.initializer->value)) {
                if (g.type.kind != Kind::Pointer) throw LoweringError("C codegen: invalid global string initializer");
                out << "static const unsigned char *" << name << " = " << string(std::get<std::string>(g.initializer->value)) << ";\n";
                continue;
            }
            auto size = type_size(g.type);
            out << "static _Alignas(16) unsigned char " << name << '[' << std::max<std::size_t>(1, size) << "] = {";
            if (!g.initializer || std::holds_alternative<std::monostate>(g.initializer->value)) out << '0';
            else {
                if (aggregate(g.type)) throw LoweringError("C codegen: only zero aggregate initializers are supported");
                uint64_t value = std::visit([](const auto& x) -> uint64_t {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, double>) return std::bit_cast<uint64_t>(x);
                    else if constexpr (std::is_integral_v<T>) return static_cast<uint64_t>(x);
                    else return 0;
                }, g.initializer->value);
                for (std::size_t i = 0; i < size; ++i) out << (i ? ", " : "") << ((value >> (i * 8)) & 255);
            }
            out << "};\n";
        }
    }
public:
    explicit CEmitter(const TranslationUnit& unit) : unit_(unit) {
        const std::map<std::string, std::string> runtime = {
            {"prints", "vm_rt_print"}, {"printi", "vm_rt_print"}, {"printf", "vm_rt_printf"},
            {"open", "vm_rt_open"}, {"read", "vm_rt_read"}, {"write", "vm_rt_write"},
            {"close", "vm_rt_close"}, {"malloc", "vm_rt_malloc"}, {"free", "vm_rt_free"}
        };
        for (std::size_t i = 0; i < unit.functions.size(); ++i) {
            const auto& f = unit.functions[i];
            if (!f.external) functions_[f.name] = "vm_function_" + std::to_string(i);
            else if (auto it = runtime.find(f.name); it != runtime.end()) functions_[f.name] = it->second;
        }
        for (std::size_t i = 0; i < unit.globals.size(); ++i)
            globals_[unit.globals[i].name] = "vm_global_" + std::to_string(i);
    }
    void run(std::ostream& out) {
        auto main = std::find_if(unit_.functions.begin(), unit_.functions.end(), [](const auto& f) {return f.name == "main" && !f.external;});
        if (main == unit_.functions.end()) throw LoweringError("C codegen: no main function");
        if (main->parameter_types.size() > 2) throw LoweringError("C codegen: main accepts at most argc and argv");
        if (main->result.kind != Kind::Int && main->result.kind != Kind::Void)
            throw LoweringError("C codegen: main must return i32 or void");
        if ((!main->parameter_types.empty() && main->parameter_types[0] != hir::Type::int_type()) ||
            (main->parameter_types.size() == 2 && main->parameter_types[1] !=
                hir::Type::pointer(hir::Type::pointer(hir::Type::char_type()))))
            throw LoweringError("C codegen: main parameters must be i32 argc, ptr ptr char argv");
        for (const auto& f : unit_.functions) if (!f.external) function(f);
        // Build global definitions first: string initializers may add literals.
        std::ostringstream data; globals(data);
        out << "/* Generated C for the 64-bit little-endian register VM. */\n#include \"vm.h\"\n\n";
        for (const auto& [value, index] : words_) {
            out << "static const uint64_t vm_constant_" << index << " = 0x" << std::hex << value << "ULL;\n" << std::dec;
        }
        for (const auto& [value, index] : strings_) {
            out << "static const unsigned char vm_string_" << index << "[] = {";
            for (unsigned char ch : value) out << static_cast<unsigned>(ch) << ", ";
            out << "0};\n";
        }
        out << data.str() << '\n';
        for (const auto& f : unit_.functions) if (!f.external)
            out << "static void " << functions_.at(f.name) << "(void);\n";
        out << '\n' << body_.str() << "VM_MAIN(" << functions_.at(main->name) << ")\n";
    }
};
} // namespace

void emit_c(const TranslationUnit& unit, std::ostream& out) { CEmitter(unit).run(out); }

} // namespace lir
