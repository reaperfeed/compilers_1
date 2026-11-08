#include <catch2/catch_test_macros.hpp>
#include "optimizer.hpp"
#include <algorithm>
#include <limits>
#include <sstream>

namespace {
using namespace hir;
struct Fixture {
    TranslationUnit u;
    Fixture() { Function f; f.name = "test"; f.result = Type::int_type(); u.functions.push_back(f); }
    Function& f() { return u.functions[0]; }
    ObjectId obj(Type t = Type::int_type()) { return f().add_object("v", t); }
    void emit(Operation o) { f().operations.push_back(o); }
    void number(ObjectId id, int64_t n) { emit(op(Opcode::Constant, id, Constant{Type::int_type(), n})); }
    void run() { build_cfg(u); optimize_local(u); }
    std::size_t count(Opcode code) { return std::count_if(f().operations.begin(), f().operations.end(), [=](auto& o) { return o.opcode == code; }); }
};
}

TEST_CASE("Local optimizer: propagates copies and folds a constant chain") {
    Fixture x; auto a=x.obj(), b=x.obj(), c=x.obj(), r=x.obj();
    x.number(a, 2); x.emit(op(Opcode::Copy,b,a)); x.number(c,3);
    x.emit(op(Opcode::Add,r,b,c)); x.emit(op(Opcode::Return,r)); x.run();
    REQUIRE(x.f().operations.size() == 3); // label, constant, return
    REQUIRE(std::get<int64_t>(std::get<Constant>(x.f().operations[1].args[1]).value) == 5);
    std::ostringstream before, after; dump(x.u,before); optimize_local(x.u); dump(x.u,after);
    REQUIRE(before.str() == after.str());
}

TEST_CASE("Local optimizer: integer identities work with an unknown parameter") {
    for (auto code : {Opcode::Add, Opcode::Sub, Opcode::Mul, Opcode::Div}) {
        Fixture x;
        auto a=x.f().add_object("arg",Type::int_type(),Storage::Parameter);
        x.f().parameter_types={Type::int_type()};
        auto b=x.obj(), r=x.obj(); x.number(b,code==Opcode::Add || code==Opcode::Sub ? 0 : 1);
        x.emit(op(code,r,a,b)); x.emit(op(Opcode::Return,r)); x.run();
        REQUIRE(x.count(code)==0); REQUIRE(x.count(Opcode::Copy)==1);
    }
}

TEST_CASE("Local optimizer: removes overwritten values and dead chains") {
    Fixture x; auto a=x.obj(), b=x.obj(), dead=x.obj();
    x.number(a,1); x.number(b,3); x.emit(op(Opcode::Mul,dead,a,b));
    x.number(a,7); x.emit(op(Opcode::Return,a)); x.run();
    REQUIRE(x.count(Opcode::Constant)==1); REQUIRE(x.count(Opcode::Mul)==0);
}

TEST_CASE("Local optimizer: redefinitions invalidate constants") {
    Fixture x; auto a=x.obj(), one=x.obj(), r=x.obj();
    auto p=x.f().add_object("arg",Type::int_type(),Storage::Parameter); x.f().parameter_types={Type::int_type()};
    x.number(a,5); x.emit(op(Opcode::Copy,a,p)); x.number(one,1);
    x.emit(op(Opcode::Add,r,a,one)); x.emit(op(Opcode::Return,r)); x.run();
    REQUIRE(x.count(Opcode::Add)==1);
}

TEST_CASE("Local optimizer: preserves values across block boundaries") {
    Fixture x; auto a=x.obj(), b=x.obj(), r=x.obj();
    x.number(a,2); x.emit(op(Opcode::Jump,LabelId{1})); x.emit(op(Opcode::Label,LabelId{1}));
    x.number(b,3); x.emit(op(Opcode::Add,r,a,b)); x.emit(op(Opcode::Return,r)); x.run();
    REQUIRE(x.count(Opcode::Add)==1); REQUIRE(x.count(Opcode::Constant)==2);
}

TEST_CASE("Local optimizer: aliases cannot make stale constants") {
    Fixture x; auto a=x.obj(), b=x.obj(), r=x.obj(), p=x.obj(Type::pointer(Type::int_type()));
    x.f().objects[a.index].storage = Storage::Local;
    x.number(a,2); x.emit(op(Opcode::AddressOf,p,a)); x.number(b,9);
    x.emit(op(Opcode::Store,p,b)); x.emit(op(Opcode::Add,r,a,b)); x.emit(op(Opcode::Return,r)); x.run();
    REQUIRE(x.count(Opcode::Store)==1); REQUIRE(x.count(Opcode::Add)==1);
    REQUIRE(x.count(Opcode::Constant)==2);
}

TEST_CASE("Local optimizer: preserves unused division by zero and overflow division") {
    for (int64_t divisor : {int64_t{0}, int64_t{-1}}) {
        Fixture x; auto a=x.obj(), b=x.obj(), dead=x.obj(), r=x.obj();
        x.number(a,std::numeric_limits<int64_t>::min()); x.number(b,divisor);
        x.emit(op(Opcode::Div,dead,a,b)); x.number(r,0); x.emit(op(Opcode::Return,r)); x.run();
        REQUIRE(x.count(Opcode::Div)==1);
    }
}

TEST_CASE("Local optimizer: avoids signed overflow during folding") {
    for (auto code : {Opcode::Add,Opcode::Sub,Opcode::Mul}) {
        Fixture x; auto a=x.obj(), b=x.obj(), r=x.obj();
        x.number(a,std::numeric_limits<int64_t>::max()); x.number(b,code==Opcode::Sub ? -2 : 2);
        x.emit(op(code,r,a,b)); x.emit(op(Opcode::Return,r)); x.run(); REQUIRE(x.count(code)==1);
    }
}

TEST_CASE("Local optimizer: floating point identities are left alone") {
    Fixture x; auto a=x.obj(Type::float_type()), b=x.obj(Type::float_type()), r=x.obj(Type::float_type());
    x.f().result=Type::float_type();
    x.emit(op(Opcode::Constant,a,Constant{Type::float_type(),0.0}));
    x.emit(op(Opcode::Constant,b,Constant{Type::float_type(),1.0}));
    x.emit(op(Opcode::Mul,r,a,b)); x.emit(op(Opcode::Return,r)); x.run(); REQUIRE(x.count(Opcode::Mul)==1);
}

TEST_CASE("Local optimizer: preserves global writes and unused calls") {
    Fixture x; auto g=x.u.add_global("global",Type::int_type()); auto r=x.obj();
    Function callee; callee.name="side_effect"; callee.external=true; callee.result=Type::int_type();
    x.u.functions.push_back(callee);
    x.number(g,7); x.emit(op(Opcode::Call,r,FunctionId{1}));
    x.emit(op(Opcode::Return,g)); x.run();
    REQUIRE(x.count(Opcode::Call)==1); REQUIRE(x.count(Opcode::Constant)==1);
}

TEST_CASE("Local optimizer: dead multiplication by zero does not delete a load") {
    Fixture x;
    auto p=x.f().add_object("ptr",Type::pointer(Type::int_type()),Storage::Parameter);
    x.f().parameter_types={Type::pointer(Type::int_type())};
    auto loaded=x.obj(), zero=x.obj(), r=x.obj();
    x.emit(op(Opcode::Load,loaded,p)); x.number(zero,0);
    x.emit(op(Opcode::Mul,r,loaded,zero)); x.emit(op(Opcode::Return,r)); x.run();
    REQUIRE(x.count(Opcode::Mul)==0); REQUIRE(x.count(Opcode::Load)==1);
}
