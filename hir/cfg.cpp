#include "hir.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <unordered_map>
#include <unordered_set>

namespace hir {
void build_cfg(Function& f) {
    if (f.external) { f.cfg.blocks.clear(); return; }
    std::unordered_set<std::size_t> occupied;
    for (const auto& o : f.operations) {
        if (o.opcode == Opcode::Label) {
            const auto* label = std::get_if<LabelId>(&o.args[0]);
            if (!label) throw ValidationError(f.name + ": label requires a label operand");
            if (!occupied.insert(label->index).second) throw ValidationError(f.name + ": duplicate label");
        }
    }
    std::size_t next = 0;
    auto fresh = [&]() {
        while (occupied.contains(next)) {
            if (next == std::numeric_limits<std::size_t>::max()) throw ValidationError("no free label IDs");
            ++next;
        }
        occupied.insert(next);
        return LabelId{next};
    };
    std::vector<Operation> normalized;
    for (const auto& o : f.operations) {
        if (o.opcode == Opcode::Label) {
            if (!normalized.empty() && !is_terminator(normalized.back().opcode))
                normalized.push_back(op(Opcode::Jump, o.args[0]));
        } else if (normalized.empty() || is_terminator(normalized.back().opcode)) {
            normalized.push_back(op(Opcode::Label, fresh()));
        }
        normalized.push_back(o);
    }
    // Every call has an explicit continuation block, even at the end of input.
    if (normalized.empty() || normalized.back().opcode == Opcode::Call || normalized.back().opcode == Opcode::CallIndirect)
        normalized.push_back(op(Opcode::Label, fresh()));
    if (!is_terminator(normalized.back().opcode))
        normalized.push_back(op(f.result.kind == TypeKind::Void ? Opcode::Return : Opcode::Unreachable));
    f.operations = std::move(normalized);
    f.cfg.blocks.clear();
    std::unordered_map<std::size_t, std::size_t> labels;
    for (std::size_t i = 0; i < f.operations.size(); ++i) {
        if (f.operations[i].opcode != Opcode::Label) continue;
        if (!f.cfg.blocks.empty()) f.cfg.blocks.back().end = i;
        const auto label = std::get<LabelId>(f.operations[i].args[0]);
        labels.emplace(label.index, f.cfg.blocks.size());
        f.cfg.blocks.push_back(BasicBlock{label, i, f.operations.size(), {}, {}});
    }
    auto target = [&](const Operand& a) {
        const auto* label = std::get_if<LabelId>(&a);
        if (!label || !labels.contains(label->index)) throw ValidationError(f.name + ": undefined jump target");
        return labels.at(label->index);
    };
    for (std::size_t b = 0; b < f.cfg.blocks.size(); ++b) {
        auto& block = f.cfg.blocks[b];
        const auto& last = f.operations[block.end - 1];
        switch (last.opcode) {
        case Opcode::Jump: block.successors.push_back({target(last.args[0]), EdgeKind::Jump}); break;
        case Opcode::Branch:
            block.successors.push_back({target(last.args[1]), EdgeKind::True});
            block.successors.push_back({target(last.args[2]), EdgeKind::False}); break;
        case Opcode::Call: case Opcode::CallIndirect:
            if (b + 1 >= f.cfg.blocks.size()) throw ValidationError(f.name + ": missing call continuation");
            block.successors.push_back({b + 1, EdgeKind::CallContinuation}); break;
        case Opcode::Return: case Opcode::Unreachable: break;
        default: throw ValidationError(f.name + ": block does not end with a terminator");
        }
        for (const auto& edge : block.successors) {
            auto& predecessors = f.cfg.blocks[edge.block].predecessors;
            if (std::find(predecessors.begin(), predecessors.end(), b) == predecessors.end())
                predecessors.push_back(b);
        }
    }
}
void build_cfg(TranslationUnit& unit) { for (auto& f : unit.functions) build_cfg(f); }
namespace {
std::string escape_dot(const std::string& value) {
    std::string result;
    for (char c : value) {
        if (c == '"' || c == '\\') result += '\\';
        if (c == '\n') result += "\\l";
        else if (c == '\r') result += "\\r";
        else result += c;
    }
    return result;
}
const char* edge_name(EdgeKind k) {
    switch (k) {
    case EdgeKind::Jump: return "jump";
    case EdgeKind::True: return "true";
    case EdgeKind::False: return "false";
    case EdgeKind::CallContinuation: return "return from call";
    }
    return "?";
}
}
void dump_cfg_dot(const TranslationUnit& unit, std::ostream& out) {
    out << "digraph CFG {\n  node [shape=box, fontname=\"monospace\"];\n";
    for (std::size_t fi = 0; fi < unit.functions.size(); ++fi) {
        const auto& f = unit.functions[fi];
        if (f.external) continue;
        out << "  subgraph cluster_" << fi << " {\n    label=\"" << escape_dot(f.name) << "\";\n";
        for (std::size_t bi = 0; bi < f.cfg.blocks.size(); ++bi) {
            const auto& b = f.cfg.blocks[bi];
            std::string label;
            for (std::size_t oi = b.begin; oi < b.end; ++oi) label += operation_text(f.operations[oi]) + '\n';
            out << "    f" << fi << "b" << bi << " [label=\"" << escape_dot(label) << "\"];\n";
            for (const auto& edge : b.successors)
                out << "    f" << fi << "b" << bi << " -> f" << fi << "b" << edge.block
                    << " [label=\"" << edge_name(edge.kind) << "\"];\n";
        }
        out << "  }\n";
    }
    out << "}\n";
}
} // namespace hir
