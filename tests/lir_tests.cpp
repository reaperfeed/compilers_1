#include <catch2/catch_test_macros.hpp>

#include "lir/lowering.hpp"

#include <sstream>

namespace {
using namespace hir;

struct Fixture {
    TranslationUnit unit;
    Fixture(Type result = Type::int_type()) {
        Function function;
        function.name = "test";
        function.result = std::move(result);
        unit.functions.push_back(std::move(function));
    }
    Function& function() { return unit.functions[0]; }
    ObjectId object(Type type, Storage storage = Storage::Temporary, std::string name = "v") {
        return function().add_object(std::move(name) + std::to_string(function().objects.size()),
                                     std::move(type), storage);
    }
    void emit(Operation operation) { function().operations.push_back(std::move(operation)); }
    lir::TranslationUnit lower() { build_cfg(unit); return lir::lower(unit); }
};

std::string text(const lir::TranslationUnit& unit) {
    std::ostringstream out;
    lir::dump(unit, out);
    return out.str();
}
}

TEST_CASE("LIR: scalar objects use virtual registers and aggregates use frame slots") {
    Fixture x;
    auto scalar = x.object(Type::int_type(), Storage::Local, "scalar");
    auto array = x.object(Type::array(Type::int_type(), 2), Storage::Local, "array");
    auto address_taken = x.object(Type::int_type(), Storage::Local, "address_taken");
    auto address = x.object(Type::pointer(Type::int_type()));
    auto loaded = x.object(Type::int_type());
    x.emit(op(Opcode::Constant, scalar, Constant{Type::int_type(), int64_t{4}}));
    x.emit(op(Opcode::Constant, array, Constant{x.function().objects[array.index].type, {}}));
    x.emit(op(Opcode::Constant, address_taken, Constant{Type::int_type(), int64_t{8}}));
    x.emit(op(Opcode::AddressOf, address, address_taken));
    x.emit(op(Opcode::Load, loaded, address));
    x.emit(op(Opcode::Return, loaded));

    auto low = x.lower();
    const auto& function = low.functions[0];
    REQUIRE(std::holds_alternative<lir::Register>(function.objects[scalar.index].location));
    REQUIRE(std::holds_alternative<lir::FrameSlot>(function.objects[array.index].location));
    REQUIRE(std::holds_alternative<lir::FrameSlot>(function.objects[address_taken.index].location));
    REQUIRE(function.frame_size >= 32);
    REQUIRE(text(low).find("bp -") != std::string::npos);
    REQUIRE(text(low).find("lea") != std::string::npos);
    REQUIRE(text(low).find("ld") != std::string::npos);
}

TEST_CASE("LIR: taking an object's address moves it from a virtual register to the stack") {
    Fixture x;
    auto local = x.object(Type::int_type(), Storage::Local, "local");
    auto pointer = x.object(Type::pointer(Type::int_type()));
    x.emit(op(Opcode::Constant, local, Constant{Type::int_type(), int64_t{9}}));
    x.emit(op(Opcode::AddressOf, pointer, local));
    x.emit(op(Opcode::Return, local));
    auto low = x.lower();
    REQUIRE(std::holds_alternative<lir::FrameSlot>(low.functions[0].objects[local.index].location));
    REQUIRE(text(low).find("lea") != std::string::npos);
}

TEST_CASE("LIR: parameters use registers and frame slots") {
    Fixture x;
    x.function().parameter_types = {Type::int_type(), Type::int_type()};
    auto register_parameter = x.object(Type::int_type(), Storage::Parameter, "register_parameter");
    auto stack_parameter = x.object(Type::int_type(), Storage::Parameter, "stack_parameter");
    auto address = x.object(Type::pointer(Type::int_type()));
    x.emit(op(Opcode::AddressOf, address, stack_parameter));
    x.emit(op(Opcode::Return, register_parameter));

    auto low = x.lower();
    REQUIRE(std::holds_alternative<lir::Register>(low.functions[0].objects[register_parameter.index].location));
    REQUIRE(std::holds_alternative<lir::FrameSlot>(low.functions[0].objects[stack_parameter.index].location));
    auto output = text(low);
    REQUIRE(output.find("getarg %v") != std::string::npos);
    REQUIRE(output.find("getarg qword [bp -") != std::string::npos);
}

TEST_CASE("LIR: array indexing expands into scaled index and address addition") {
    Fixture x;
    auto array = x.object(Type::array(Type::int_type(), 3), Storage::Local, "array");
    auto index = x.object(Type::int_type());
    auto pointer = x.object(Type::pointer(Type::int_type()));
    auto value = x.object(Type::int_type());
    x.emit(op(Opcode::Constant, array, Constant{x.function().objects[array.index].type, {}}));
    x.emit(op(Opcode::Constant, index, Constant{Type::int_type(), int64_t{1}}));
    x.emit(op(Opcode::ElementAddress, pointer, array, index));
    x.emit(op(Opcode::Constant, value, Constant{Type::int_type(), int64_t{22}}));
    x.emit(op(Opcode::Store, pointer, value));
    x.emit(op(Opcode::Return, value));

    auto output = text(x.lower());
    REQUIRE(output.find("mul") != std::string::npos);
    REQUIRE(output.find("add") != std::string::npos);
    REQUIRE(output.find("st") != std::string::npos);
}

TEST_CASE("LIR: structure fields use padded byte offsets and ordinary load/store") {
    auto pair = Type::structure("Pair", {{"tag", Type::char_type()}, {"value", Type::int_type()}});
    REQUIRE(lir::field_offset(pair, 0) == 0);
    REQUIRE(lir::field_offset(pair, 1) == 8);
    REQUIRE(lir::type_size(pair) == 16);

    Fixture x;
    auto structure = x.object(pair, Storage::Local, "pair");
    auto field_pointer = x.object(Type::pointer(Type::int_type()));
    auto value = x.object(Type::int_type());
    x.emit(op(Opcode::Constant, structure, Constant{pair, {}}));
    x.emit(op(Opcode::FieldAddress, field_pointer, structure, FieldIndex{1}));
    x.emit(op(Opcode::Constant, value, Constant{Type::int_type(), int64_t{5}}));
    x.emit(op(Opcode::Store, field_pointer, value));
    x.emit(op(Opcode::Return, value));

    auto output = text(x.lower());
    REQUIRE(output.find("add") != std::string::npos);
    REQUIRE(output.find(", 8") != std::string::npos);
    REQUIRE(output.find("st qword") != std::string::npos);
}

TEST_CASE("LIR: aggregate pointer access uses scalar loads and stores") {
    Fixture x;
    auto array_type = Type::array(Type::int_type(), 2);
    auto source = x.object(array_type, Storage::Local, "source");
    auto destination = x.object(array_type, Storage::Local, "destination");
    auto pointer = x.object(Type::pointer(array_type));
    auto zero = x.object(Type::int_type());
    x.emit(op(Opcode::Constant, source, Constant{array_type, {}}));
    x.emit(op(Opcode::AddressOf, pointer, source));
    x.emit(op(Opcode::Load, destination, pointer));
    x.emit(op(Opcode::Store, pointer, destination));
    x.emit(op(Opcode::Constant, zero, Constant{Type::int_type(), int64_t{0}}));
    x.emit(op(Opcode::Return, zero));

    auto output = text(x.lower());
    REQUIRE(output.find("ld %v") != std::string::npos);
    REQUIRE(output.find("st qword") != std::string::npos);
}

TEST_CASE("LIR: function arguments and results become explicit calling-convention steps") {
    Fixture x;
    Function callee;
    callee.name = "callee";
    callee.result = Type::int_type();
    callee.parameter_types = {Type::int_type()};
    callee.external = true;
    x.unit.functions.push_back(callee);
    auto argument = x.object(Type::int_type());
    auto result = x.object(Type::int_type());
    x.emit(op(Opcode::Constant, argument, Constant{Type::int_type(), int64_t{3}}));
    auto call = op(Opcode::Call, result, FunctionId{1});
    call.arguments = {argument};
    x.emit(std::move(call));
    x.emit(op(Opcode::Return, result));

    auto output = text(x.lower());
    REQUIRE(output.find("setarg 0") != std::string::npos);
    REQUIRE(output.find("call @f:callee") != std::string::npos);
    REQUIRE(output.find("getret") != std::string::npos);
    REQUIRE(output.find("setret") != std::string::npos);
    REQUIRE(output.find("mov bp, sp") != std::string::npos);
    REQUIRE(output.find("mov sp, bp") != std::string::npos);
}

TEST_CASE("LIR: globals are addressed by symbol and loaded from memory") {
    Fixture x;
    auto global = x.unit.add_global("counter", Type::int_type(), Constant{Type::int_type(), int64_t{7}});
    x.emit(op(Opcode::Return, global));
    auto output = text(x.lower());
    REQUIRE(output.find("global @g:counter") != std::string::npos);
    REQUIRE(output.find("lea") != std::string::npos);
    REQUIRE(output.find("ld %v") != std::string::npos);
}

TEST_CASE("LIR: conversions and comparisons become typed scalar operations") {
    Fixture x(Type::void_type());
    auto integer = x.object(Type::int_type());
    auto floating = x.object(Type::float_type());
    auto boolean = x.object(Type::bool_type());
    auto zero = x.object(Type::float_type());
    x.emit(op(Opcode::Constant, integer, Constant{Type::int_type(), int64_t{2}}));
    x.emit(op(Opcode::Convert, floating, integer));
    x.emit(op(Opcode::Constant, zero, Constant{Type::float_type(), 0.0}));
    x.emit(op(Opcode::Lt, boolean, floating, zero));
    x.emit(op(Opcode::Return));
    auto output = text(x.lower());
    REQUIRE(output.find("convert") != std::string::npos);
    REQUIRE(output.find("cmp_lt") != std::string::npos);
}

