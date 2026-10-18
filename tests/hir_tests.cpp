#include <catch2/catch_test_macros.hpp>
#include "hir.hpp"

#include <algorithm>
#include <sstream>

namespace {
using namespace hir;
struct Fixture {
    TranslationUnit unit;
    Fixture(Type result = Type::int_type()) {
        Function f; f.name = "test"; f.result = std::move(result);
        unit.functions.push_back(std::move(f));
    }
    Function& f() { return unit.functions[0]; }
    ObjectId object(Type type = Type::int_type(), Storage storage = Storage::Temporary) {
        return f().add_object("v" + std::to_string(f().objects.size()), std::move(type), storage);
    }
    void emit(Operation o) { f().operations.push_back(std::move(o)); }
    void ok() { build_cfg(unit); verify(unit); }
};
}

TEST_CASE("HIR: constants, arithmetic and serialization") {
    Fixture x;
    auto a = x.object(), b = x.object(), c = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{2}}));
    x.emit(op(Opcode::Constant, b, Constant{Type::int_type(), int64_t{3}}));
    x.emit(op(Opcode::Add, c, a, b)); x.emit(op(Opcode::Return, c));
    REQUIRE_NOTHROW(x.ok());
    std::ostringstream out; dump(x.unit, out);
    REQUIRE(out.str().find("add %2, %0, %1") != std::string::npos);
}
TEST_CASE("HIR: all arithmetic and comparison opcodes") {
    for (auto code : {Opcode::Add, Opcode::Sub, Opcode::Mul, Opcode::Div, Opcode::Mod,
                      Opcode::BitAnd, Opcode::BitOr, Opcode::BitXor, Opcode::Shl, Opcode::Shr,
                      Opcode::Eq, Opcode::Ne, Opcode::Lt, Opcode::Gt, Opcode::Le, Opcode::Ge}) {
        Fixture x;
        auto a = x.object(), b = x.object();
        bool comparison = code >= Opcode::Eq && code <= Opcode::Ge;
        auto r = x.object(comparison ? Type::bool_type() : Type::int_type());
        x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{7}}));
        x.emit(op(Opcode::Constant, b, Constant{Type::int_type(), int64_t{2}}));
        x.emit(op(code, r, a, b)); x.emit(op(Opcode::Return, a));
        REQUIRE_NOTHROW(x.ok());
    }
}
TEST_CASE("HIR: unary and conversion operations") {
    Fixture x(Type::void_type());
    auto a = x.object(), r = x.object(), b = x.object(Type::bool_type()), br = x.object(Type::bool_type());
    auto fl = x.object(Type::float_type());
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{3}}));
    x.emit(op(Opcode::Constant, b, Constant{Type::bool_type(), true}));
    x.emit(op(Opcode::Neg, r, a)); x.emit(op(Opcode::BitNot, r, a));
    x.emit(op(Opcode::LogicalNot, br, b)); x.emit(op(Opcode::Convert, fl, a));
    x.emit(op(Opcode::Return)); REQUIRE_NOTHROW(x.ok());
}
TEST_CASE("HIR: implicit mixed arithmetic is rejected") {
    Fixture x;
    auto a = x.object(), b = x.object(Type::float_type()), r = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}}));
    x.emit(op(Opcode::Constant, b, Constant{Type::float_type(), 2.0}));
    x.emit(op(Opcode::Add, r, a, b)); x.emit(op(Opcode::Return, r));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: constant type and payload must agree") {
    Fixture x; auto a = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), 1.0})); x.emit(op(Opcode::Return, a));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: undefined values are rejected") {
    Fixture x; auto a = x.object(), r = x.object();
    x.emit(op(Opcode::Copy, r, a));
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}})); x.emit(op(Opcode::Return, r));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: a definition in one branch is insufficient") {
    Fixture x;
    auto flag = x.object(Type::bool_type(), Storage::Parameter); x.f().parameter_types = {Type::bool_type()};
    auto a = x.object();
    x.emit(op(Opcode::Branch, flag, LabelId{1}, LabelId{2}));
    x.emit(op(Opcode::Label, LabelId{1}));
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}})); x.emit(op(Opcode::Jump, LabelId{2}));
    x.emit(op(Opcode::Label, LabelId{2})); x.emit(op(Opcode::Return, a));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: both branch definitions reach the join") {
    Fixture x;
    auto flag = x.object(Type::bool_type(), Storage::Parameter); x.f().parameter_types = {Type::bool_type()};
    auto a = x.object();
    x.emit(op(Opcode::Branch, flag, LabelId{1}, LabelId{2}));
    for (std::size_t label : {1, 2}) {
        x.emit(op(Opcode::Label, LabelId{label}));
        x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), static_cast<int64_t>(label)})); x.emit(op(Opcode::Jump, LabelId{3}));
    }
    x.emit(op(Opcode::Label, LabelId{3})); x.emit(op(Opcode::Return, a));
    REQUIRE_NOTHROW(x.ok());
}
TEST_CASE("HIR: loop definition cannot initialize a value before the first iteration") {
    Fixture x;
    auto flag = x.object(Type::bool_type(), Storage::Parameter); x.f().parameter_types = {Type::bool_type()};
    auto a = x.object();
    x.emit(op(Opcode::Label, LabelId{0})); x.emit(op(Opcode::Branch, flag, LabelId{1}, LabelId{2}));
    x.emit(op(Opcode::Label, LabelId{1}));
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{2}})); x.emit(op(Opcode::Jump, LabelId{0}));
    x.emit(op(Opcode::Label, LabelId{2})); x.emit(op(Opcode::Return, a));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: initialized loop values pass fixed point analysis") {
    Fixture x;
    auto flag = x.object(Type::bool_type(), Storage::Parameter); x.f().parameter_types = {Type::bool_type()};
    auto a = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}}));
    x.emit(op(Opcode::Label, LabelId{1})); x.emit(op(Opcode::Add, a, a, a));
    x.emit(op(Opcode::Branch, flag, LabelId{1}, LabelId{2}));
    x.emit(op(Opcode::Label, LabelId{2})); x.emit(op(Opcode::Return, a)); REQUIRE_NOTHROW(x.ok());
}
TEST_CASE("HIR: typed globals and default initialization") {
    Fixture x; auto g = x.unit.add_global("counter", Type::int_type());
    x.emit(op(Opcode::Return, g)); REQUIRE_NOTHROW(x.ok());
    x.unit.globals[0].initializer = Constant{Type::float_type(), 1.0};
    REQUIRE_THROWS_AS(verify(x.unit), ValidationError);
}
TEST_CASE("HIR: address, store and load") {
    Fixture x;
    auto v = x.object(Type::int_type(), Storage::Local), r = x.object(), p = x.object(Type::pointer(Type::int_type()));
    x.emit(op(Opcode::Constant, v, Constant{Type::int_type(), int64_t{9}}));
    x.emit(op(Opcode::AddressOf, p, v)); x.emit(op(Opcode::Store, p, v));
    x.emit(op(Opcode::Load, r, p)); x.emit(op(Opcode::Return, r)); REQUIRE_NOTHROW(x.ok());
    x.f().objects[p.index].type = Type::pointer(Type::char_type()); REQUIRE_THROWS_AS(verify(x.unit), ValidationError);
}
TEST_CASE("HIR: element and structure field addresses") {
    Fixture x;
    auto aggregate = Type::structure("Pair", {{"x", Type::int_type()}, {"y", Type::float_type()}});
    auto arr = x.object(Type::array(Type::int_type(), 4), Storage::Local);
    auto s = x.object(aggregate, Storage::Local), index = x.object();
    auto p = x.object(Type::pointer(Type::int_type())), r = x.object();
    x.emit(op(Opcode::Constant, arr, Constant{x.f().objects[arr.index].type, {}}));
    x.emit(op(Opcode::Constant, s, Constant{aggregate, {}}));
    x.emit(op(Opcode::Constant, index, Constant{Type::int_type(), int64_t{0}}));
    x.emit(op(Opcode::ElementAddress, p, arr, index));
    x.emit(op(Opcode::FieldAddress, p, s, FieldIndex{0}));
    x.emit(op(Opcode::Load, r, p)); x.emit(op(Opcode::Return, r)); REQUIRE_NOTHROW(x.ok());
    x.f().operations[5].args[2] = FieldIndex{9}; REQUIRE_THROWS_AS(verify(x.unit), ValidationError);
}
TEST_CASE("HIR: direct calls check arguments and return types") {
    Fixture x;
    Function target; target.name = "callee"; target.external = true;
    target.result = Type::int_type(); target.parameter_types = {Type::int_type()};
    x.unit.functions.push_back(target);
    auto a = x.object(), r = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}}));
    auto call = op(Opcode::Call, r, FunctionId{1}); call.arguments = {a}; x.emit(call);
    x.emit(op(Opcode::Return, r)); REQUIRE_NOTHROW(x.ok());
    REQUIRE(x.f().cfg.blocks.size() == 2);
    REQUIRE(x.f().cfg.blocks[0].successors[0].kind == EdgeKind::CallContinuation);
    x.f().operations[2].arguments.clear(); REQUIRE_THROWS_AS(verify(x.unit), ValidationError);
}
TEST_CASE("HIR: indirect call signature is checked") {
    Fixture x;
    auto signature = Type::function(Type::int_type(), {Type::int_type()});
    auto p = x.object(Type::pointer(signature), Storage::Parameter); x.f().parameter_types = {Type::pointer(signature)};
    auto a = x.object(), r = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{2}}));
    auto call = op(Opcode::CallIndirect, r, p); call.arguments = {a}; x.emit(call);
    x.emit(op(Opcode::Return, r)); REQUIRE_NOTHROW(x.ok());
}
TEST_CASE("HIR: void calls have no destination") {
    Fixture x(Type::void_type());
    Function target; target.name = "callee"; target.external = true;
    x.unit.functions.push_back(target);
    x.emit(op(Opcode::Call, {}, FunctionId{1})); x.emit(op(Opcode::Return));
    REQUIRE_NOTHROW(x.ok());
}
TEST_CASE("HIR: invalid object and function IDs are rejected") {
    Fixture x; x.emit(op(Opcode::Return, ObjectId{42})); REQUIRE_THROWS_AS(x.ok(), ValidationError);
    x.f().operations = {op(Opcode::Call, {}, FunctionId{42})}; REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: branch and return types are strict") {
    Fixture x; auto a = x.object();
    x.emit(op(Opcode::Constant, a, Constant{Type::int_type(), int64_t{1}}));
    x.emit(op(Opcode::Branch, a, LabelId{1}, LabelId{1})); x.emit(op(Opcode::Label, LabelId{1})); x.emit(op(Opcode::Return, a));
    REQUIRE_THROWS_AS(x.ok(), ValidationError);
    x.f().operations = {op(Opcode::Return)}; REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("HIR: unused operands and argument lists are rejected") {
    Fixture x(Type::void_type()); auto o = op(Opcode::Return); o.arguments = {ObjectId{0}};
    x.emit(o); REQUIRE_THROWS_AS(x.ok(), ValidationError);
    x.f().operations = {op(Opcode::Return, {}, LabelId{1})}; REQUIRE_THROWS_AS(x.ok(), ValidationError);
}
TEST_CASE("CFG: labels are inserted and adjacent labels get jumps") {
    Fixture x(Type::void_type());
    x.emit(op(Opcode::Label, LabelId{1})); x.emit(op(Opcode::Label, LabelId{2})); x.emit(op(Opcode::Return));
    REQUIRE_NOTHROW(x.ok()); REQUIRE(x.f().cfg.blocks.size() == 2);
    REQUIRE(x.f().operations[1].opcode == Opcode::Jump);
    const auto saved = x.f().cfg.blocks; build_cfg(x.f()); REQUIRE(x.f().cfg.blocks == saved);
}
TEST_CASE("CFG: duplicate and undefined labels are rejected") {
    Fixture x(Type::void_type());
    x.emit(op(Opcode::Label, LabelId{1})); x.emit(op(Opcode::Label, LabelId{1}));
    REQUIRE_THROWS_AS(build_cfg(x.f()), ValidationError);
    x.f().operations = {op(Opcode::Jump, LabelId{99})}; REQUIRE_THROWS_AS(build_cfg(x.f()), ValidationError);
}
TEST_CASE("CFG: calls at end of input acquire continuation blocks") {
    Fixture x(Type::void_type());
    Function target; target.name = "callee"; target.external = true; x.unit.functions.push_back(target);
    x.emit(op(Opcode::Call, {}, FunctionId{1})); REQUIRE_NOTHROW(x.ok());
    REQUIRE(x.f().cfg.blocks.size() == 2); REQUIRE(x.f().operations.back().opcode == Opcode::Return);
}
TEST_CASE("CFG: stale successor lists are rejected") {
    Fixture x(Type::void_type()); x.emit(op(Opcode::Return)); x.ok();
    x.f().cfg.blocks[0].successors.push_back({99, EdgeKind::Jump}); REQUIRE_THROWS_AS(verify(x.unit), ValidationError);
}
TEST_CASE("CFG: unreachable code has a separate block") {
    Fixture x(Type::void_type()); x.emit(op(Opcode::Return)); x.emit(op(Opcode::Return));
    REQUIRE_NOTHROW(x.ok()); REQUIRE(x.f().cfg.blocks.size() == 2);
    REQUIRE(x.f().cfg.blocks[1].predecessors.empty());
    std::ostringstream out; dump_cfg_dot(x.unit, out); REQUIRE(out.str().find("digraph CFG") == 0);
}

TEST_CASE("HIR: malformed composite types are rejected") {
    auto missing_base = Type{}; missing_base.kind = TypeKind::Pointer;
    auto bad_pointer = Type::pointer(Type::int_type()); bad_pointer.extent = 3;
    auto bad_structure = Type::structure("Duplicate", {{"x", Type::int_type()}, {"x", Type::int_type()}});
    auto bad_array = Type::array(Type::void_type(), 1);
    for (const auto& bad : {missing_base, bad_pointer, bad_structure, bad_array}) {
        Fixture x(Type::void_type()); x.object(bad); x.emit(op(Opcode::Return));
        REQUIRE_THROWS_AS(x.ok(), ValidationError);
    }
}
