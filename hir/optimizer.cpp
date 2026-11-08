#include "optimizer.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace hir {
namespace {
using Integer = std::int64_t;

// No host signed overflow and no folding of operations that could trap.
std::optional<Integer> calculate(Opcode code, Integer a, Integer b) {
    const auto lo = std::numeric_limits<Integer>::min();
    const auto hi = std::numeric_limits<Integer>::max();
    switch (code) {
    case Opcode::Add:
        if ((b > 0 && a > hi - b) || (b < 0 && a < lo - b)) return {};
        return a + b;
    case Opcode::Sub:
        if ((b < 0 && a > hi + b) || (b > 0 && a < lo + b)) return {};
        return a - b;
    case Opcode::Mul:
        if (a > 0 ? (b > 0 ? a > hi / b : b < lo / a)
                  : (a < 0 && (b > 0 ? a < lo / b : b < 0 && a < hi / b))) return {};
        return a * b;
    case Opcode::Div: case Opcode::Mod:
        if (b == 0 || (a == lo && b == -1)) return {};
        return code == Opcode::Div ? a / b : a % b;
    default: return {};
    }
}

// A deliberately small whitelist: loads, conversions, division, shifts and
// pointer arithmetic remain, even if unused, because they may fail at runtime.
bool removable(const Operation& o, const Function& f, const TranslationUnit& u) {
    switch (o.opcode) {
    case Opcode::Constant: case Opcode::Copy: return true;
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
    case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor:
    case Opcode::Neg: case Opcode::BitNot: case Opcode::LogicalNot:
    case Opcode::Eq: case Opcode::Ne: case Opcode::Lt: case Opcode::Gt:
    case Opcode::Le: case Opcode::Ge:
        return u.object(f, std::get<ObjectId>(o.args[1])).type.kind != TypeKind::Pointer;
    default: return false;
    }
}

void optimize_function(Function& f, const TranslationUnit& u) {
    const auto n = f.objects.size();
    // Escaped/aggregate objects may be observed through aliases. Never track
    // their constants or delete their definitions.
    std::vector<bool> pinned(n, false);
    for (std::size_t i = 0; i < n; ++i)
        pinned[i] = !f.objects[i].type.is_scalar();
    for (const auto& o : f.operations) {
        if (o.opcode == Opcode::AddressOf || o.opcode == Opcode::FieldAddress ||
            o.opcode == Opcode::ElementAddress) {
            auto id = std::get<ObjectId>(o.args[1]);
            if (!id.global) pinned[id.index] = true;
        }
    }

    // Forward pass. HIR arithmetic operands must stay ObjectIds, so replace
    // an entire operation with Constant/Copy instead of inserting immediates.
    for (const auto& block : f.cfg.blocks) {
        std::vector<std::optional<Constant>> known(n);
        auto constant = [&](const Operand& a) -> std::optional<Constant> {
            const auto* id = std::get_if<ObjectId>(&a);
            if (!id || id->global || pinned[id->index]) return {};
            return known[id->index];
        };
        for (std::size_t i = block.begin; i < block.end; ++i) {
            auto& o = f.operations[i];
            const auto dst = definition(o);
            if (!dst) continue;
            auto left = constant(o.args[1]), right = constant(o.args[2]);
            auto replace_constant = [&](Constant value) {
                o = op(Opcode::Constant, *dst, std::move(value), {}, o.loc);
            };
            auto replace_copy = [&](Operand source) {
                o = op(Opcode::Copy, *dst, std::move(source), {}, o.loc);
            };
            if (o.opcode == Opcode::Copy && left) replace_constant(*left);
            else if (u.object(f, *dst).type.kind == TypeKind::Int) {
                const auto* a = left ? std::get_if<Integer>(&left->value) : nullptr;
                const auto* b = right ? std::get_if<Integer>(&right->value) : nullptr;
                auto value = a && b ? calculate(o.opcode, *a, *b) : std::nullopt;
                if (value) replace_constant(Constant{Type::int_type(), *value});
                else if ((o.opcode == Opcode::Add && b && *b == 0) ||
                         (o.opcode == Opcode::Sub && b && *b == 0) ||
                         (o.opcode == Opcode::Mul && b && *b == 1) ||
                         (o.opcode == Opcode::Div && b && *b == 1)) replace_copy(o.args[1]);
                else if ((o.opcode == Opcode::Add && a && *a == 0) ||
                         (o.opcode == Opcode::Mul && a && *a == 1)) replace_copy(o.args[2]);
                else if (o.opcode == Opcode::Mul && ((a && *a == 0) || (b && *b == 0)))
                    replace_constant(Constant{Type::int_type(), Integer{0}});
            }
            if (!dst->global && !pinned[dst->index]) {
                known[dst->index].reset(); // every redefinition kills the old fact
                if (o.opcode == Opcode::Constant)
                    known[dst->index] = std::get<Constant>(o.args[1]);
                else if (o.opcode == Opcode::Copy) known[dst->index] = constant(o.args[1]);
            }
        }
    }

    // Backward pass, local DCE. Conservatively keep any value used in another
    // block; no global liveness fixed point is needed for this lab.
    std::vector<bool> erase(f.operations.size(), false);
    for (const auto& block : f.cfg.blocks) {
        auto live = pinned;
        for (std::size_t i = 0; i < f.operations.size(); ++i) {
            if (i >= block.begin && i < block.end) continue;
            for (auto id : uses(f.operations[i], f, u))
                if (!id.global) live[id.index] = true;
        }
        for (std::size_t i = block.end; i-- > block.begin;) {
            const auto& o = f.operations[i];
            auto dst = definition(o);
            if (dst && !dst->global && !pinned[dst->index] &&
                !live[dst->index] && removable(o, f, u)) {
                erase[i] = true;
                continue; // erased instructions do not make their operands live
            }
            if (dst && !dst->global) live[dst->index] = pinned[dst->index];
            for (auto id : uses(o, f, u))
                if (!id.global) live[id.index] = true;
        }
    }
    std::vector<Operation> result;
    for (std::size_t i = 0; i < f.operations.size(); ++i)
        if (!erase[i]) result.push_back(std::move(f.operations[i]));
    f.operations = std::move(result);
}
}

void optimize_local(TranslationUnit& unit) {
    verify(unit);
    for (auto& f : unit.functions)
        if (!f.external) optimize_function(f, unit);
    build_cfg(unit);
    verify(unit);
}
}
