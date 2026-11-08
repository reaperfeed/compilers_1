#include "lir/c_codegen.hpp"
#include "lir/lowering.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace hir;
ObjectId value(Function& f, Type type) { return f.add_object("v", std::move(type)); }

lir::TranslationUnit aggregate_fixture() {
    TranslationUnit unit;
    auto pair = Type::structure("Pair", {{"tag", Type::char_type()}, {"number", Type::int_type()}});
    Function main; main.name = "main"; main.result = Type::int_type();
    auto source = main.add_object("source", pair, Storage::Local);
    auto result = main.add_object("result", pair, Storage::Local);
    auto pointer = value(main, Type::pointer(Type::int_type()));
    auto number = value(main, Type::int_type());
    main.operations = {
        op(Opcode::Constant, source, Constant{pair, {}}),
        op(Opcode::FieldAddress, pointer, source, FieldIndex{1}),
        op(Opcode::Constant, number, Constant{Type::int_type(), int64_t{42}}),
        op(Opcode::Store, pointer, number)
    };
    auto call = op(Opcode::Call, result, FunctionId{1}); call.arguments = {source};
    main.operations.push_back(call);
    main.operations.push_back(op(Opcode::FieldAddress, pointer, result, FieldIndex{1}));
    main.operations.push_back(op(Opcode::Load, number, pointer));
    main.operations.push_back(op(Opcode::Return, number));
    Function echo; echo.name = "echo"; echo.result = pair; echo.parameter_types = {pair};
    auto parameter = echo.add_object("pair", pair, Storage::Parameter);
    echo.operations = {op(Opcode::Return, parameter)};
    unit.functions = {main, echo}; build_cfg(unit);
    return lir::lower(unit);
}

lir::TranslationUnit global_fixture() {
    TranslationUnit unit;
    auto counter = unit.add_global("counter", Type::int_type(), Constant{Type::int_type(), int64_t{7}});
    auto message = unit.add_global("message", Type::pointer(Type::char_type()),
                                  Constant{Type::pointer(Type::char_type()), std::string{"global!"}});
    Function main; main.name = "main"; main.result = Type::int_type();
    auto increment = value(main, Type::int_type());
    auto result = value(main, Type::int_type());
    main.operations = {
        op(Opcode::Constant, increment, Constant{Type::int_type(), int64_t{35}}),
        op(Opcode::Add, counter, counter, increment),
        op(Opcode::Copy, result, counter)
    };
    Function print; print.name = "prints"; print.result = Type::int_type();
    print.external = print.variadic = true; print.builtin = Builtin::Print;
    auto ignored = value(main, Type::int_type());
    auto call = op(Opcode::Call, ignored, FunctionId{1}); call.arguments = {message};
    main.operations.push_back(call); main.operations.push_back(op(Opcode::Return, result));
    unit.functions = {main, print}; build_cfg(unit);
    return lir::lower(unit);
}

lir::Register vr(std::size_t i) { return {lir::RegisterKind::Virtual, i}; }
lir::Register bp() { return {lir::RegisterKind::BasePointer, 0}; }
lir::Register sp() { return {lir::RegisterKind::StackPointer, 0}; }
void emit(lir::Function& f, lir::Opcode opcode, std::initializer_list<lir::Operand> args,
          Type type = Type::int_type()) {
    f.instructions.push_back({opcode, std::vector<lir::Operand>(args), {}, std::move(type)});
}
void enter(lir::Function& f) {
    emit(f, lir::Opcode::Push, {bp()}); emit(f, lir::Opcode::Move, {bp(), sp()});
}
void leave(lir::Function& f) {
    emit(f, lir::Opcode::Move, {sp(), bp()}); emit(f, lir::Opcode::Pop, {bp()}); emit(f, lir::Opcode::Return, {});
}

lir::TranslationUnit indirect_fixture() {
    lir::TranslationUnit unit;
    lir::Function main; main.name = "main"; main.result = Type::int_type(); enter(main);
    emit(main, lir::Opcode::LoadAddress, {vr(0), lir::Symbol{"increment", true}});
    emit(main, lir::Opcode::LoadImmediate, {vr(1), int64_t{41}});
    emit(main, lir::Opcode::SetArgument, {int64_t{0}, vr(1)});
    emit(main, lir::Opcode::CallIndirect, {vr(0)});
    emit(main, lir::Opcode::GetReturn, {vr(2)});
    emit(main, lir::Opcode::SetReturn, {vr(2)}); leave(main);
    lir::Function inc; inc.name = "increment"; inc.result = Type::int_type(); inc.parameter_types = {Type::int_type()}; enter(inc);
    emit(inc, lir::Opcode::GetArgument, {vr(0), int64_t{0}});
    emit(inc, lir::Opcode::LoadImmediate, {vr(1), int64_t{1}});
    emit(inc, lir::Opcode::Add, {vr(2), vr(0), vr(1)});
    emit(inc, lir::Opcode::SetReturn, {vr(2)}); leave(inc);
    unit.functions = {main, inc}; return unit;
}

lir::TranslationUnit conversion_fixture() {
    lir::TranslationUnit unit;
    lir::Function f; f.name = "main"; f.result = Type::int_type(); enter(f);
    emit(f, lir::Opcode::LoadImmediate, {vr(0), -42.75});
    emit(f, lir::Opcode::Convert, {vr(1), vr(0), std::string{"i32"}}, Type::float_type());
    emit(f, lir::Opcode::Neg, {vr(2), vr(1)});
    emit(f, lir::Opcode::SetReturn, {vr(2)}); leave(f);
    unit.functions = {f}; return unit;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected fixture: aggregate/global/indirect/conversion");
        const std::string name = argv[1];
        auto unit = name == "aggregate" ? aggregate_fixture() : name == "global" ? global_fixture() :
                    name == "indirect" ? indirect_fixture() : name == "conversion" ? conversion_fixture() :
                    throw std::runtime_error("unknown fixture");
        lir::emit_c(unit, std::cout);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
