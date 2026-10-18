%language "C++"
%require "3.8"

%define api.value.type variant
%define api.token.constructor
%define api.token.prefix {TOK_}
%define api.parser.class {Parser}
%define api.namespace {yy}
%define parse.error detailed
%define parse.trace

%locations

%code requires {
    #include "ast.hpp"
    #include "errors.hpp"
    #include <cstdint>
    #include <optional>
    #include <string>
    #include <utility>
    #include <vector>
}

%code provides {
    #define YY_DECL yy::Parser::symbol_type yylex()
    YY_DECL;
}

%code {
    // Bison-локация -> С++-SourceLocation.
    static ast::SourceLocation SL(const yy::location& l) {
        return ast::SourceLocation{l.begin.line, l.begin.column};
    }
}

%parse-param { ast::Builder& b }

//токены
%token END 0 "end of file"

%token <double>      FLOAT
%token <int64_t>     NUMBER
%token <char>        CHAR_LITERAL
%token <std::string> IDENTIFIER
%token <std::string> STRING_LITERAL

%token IF ELIF ELSE WHILE FOR RETURN
%token F32 I32 VOID BOOL CHAR PTR TRUE_LIT FALSE_LIT

%token EQ NEQ LT GT LE GE
%token AMP PIPE CARET TILDE NOT
%token LSHIFT RSHIFT AND OR

%token ASSIGN PLUS_ASSIGN MINUS_ASSIGN
%token STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%token AMP_ASSIGN PIPE_ASSIGN CARET_ASSIGN
%token LSHIFT_ASSIGN RSHIFT_ASSIGN

%token PLUS MINUS STAR SLASH PERCENT

%token LPAREN RPAREN LBRACE RBRACE
%token LBRACKET RBRACKET SEMICOLON COMMA

/* нетерминалы */
%nterm <ast::NodeIt> program function type block statement
%nterm <ast::NodeIt> var_decl expr_stmt if_stmt while_stmt for_stmt return_stmt
%nterm <ast::NodeIt> expression param stmt_list

%nterm <std::vector<ast::NodeIt>> function_list param_list arg_list
%nterm <std::optional<ast::NodeIt>> elif_chain

/* приоритеты */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
        AMP_ASSIGN PIPE_ASSIGN CARET_ASSIGN LSHIFT_ASSIGN RSHIFT_ASSIGN
%left  OR
%left  AND
%left  PIPE
%left  CARET
%left  AMP
%left  EQ NEQ
%left  LT GT LE GE
%left  LSHIFT RSHIFT
%left  PLUS MINUS
%left  STAR SLASH PERCENT
%right NOT TILDE
%precedence UNARY_MINUS UNARY_AMP UNARY_STAR
%precedence PTR
%left  LPAREN LBRACKET

%start program

%%

/* программа */
program:
    function_list
    {
        auto prog = b.make(ast::NodeKind::Program, SL(@$));
        for (auto fn : $1) {
            b.link(prog, ast::EdgeRole::Func, fn);
        }
        $$ = prog;
    }
    ;

function_list:
    function                    { $$ = { $1 }; }
  | function_list function      { $1.push_back($2); $$ = std::move($1); }
    ;

function:
    type IDENTIFIER LPAREN param_list RPAREN block
    {
        auto fn = b.make(ast::NodeKind::Function, SL(@$));
        b.node(fn).value = std::move($2);
        b.link(fn, ast::EdgeRole::ReturnType, $1);
        for (auto p : $4) {
            b.link(fn, ast::EdgeRole::Param, p);
        }
        b.link(fn, ast::EdgeRole::Body, $6);
        $$ = fn;
    }
    ;

/* типы */
type:
    I32   { $$ = b.type_prim(ast::TypeKind::Int,  SL(@1)); }
  | F32   { $$ = b.type_prim(ast::TypeKind::Float, SL(@1)); }
  | VOID  { $$ = b.type_prim(ast::TypeKind::Void,  SL(@1)); }
  | BOOL  { $$ = b.type_prim(ast::TypeKind::Bool, SL(@1)); }
  | CHAR  { $$ = b.type_prim(ast::TypeKind::Char, SL(@1)); }
  | PTR type
    {
        auto n = b.make(ast::NodeKind::TypePtr, SL(@$));
        b.link(n, ast::EdgeRole::Base, $2);
        $$ = n;
    }
  | type LBRACKET RBRACKET
    {
        auto n = b.make(ast::NodeKind::TypeArray, SL(@$));
        b.link(n, ast::EdgeRole::Base, $1);
        $$ = n;
    }
  | type LBRACKET NUMBER RBRACKET
    {
        auto n = b.make(ast::NodeKind::TypeArray, SL(@$));
        b.link(n, ast::EdgeRole::Base, $1);
        b.link(n, ast::EdgeRole::ArrSize, b.number($3, SL(@3)));
        $$ = n;
    }
    ;

/* параметры */
param_list:
    /* пусто */   { $$ = {}; }
  | param          { $$ = { $1 }; }
  | param_list COMMA param
    { $1.push_back($3); $$ = std::move($1); }
    ;

param:
    type IDENTIFIER
    {
        auto p = b.make(ast::NodeKind::VarDecl, SL(@$));
        b.node(p).value = std::move($2);
        b.link(p, ast::EdgeRole::DeclType, $1);
        $$ = p;
    }
    ;

/* блоки и инструкции */
block:
    LBRACE stmt_list RBRACE    { $$ = $2; }
    ;

stmt_list:
    /* пусто */  { $$ = b.make(ast::NodeKind::Block, SL(@$)); }
  | stmt_list statement
    {
        b.link($1, ast::EdgeRole::Stmt, $2);
        $$ = $1;
    }
    ;

statement:
    var_decl
  | expr_stmt
  | if_stmt
  | while_stmt
  | for_stmt
  | return_stmt
  | block
    ;

var_decl:
    type IDENTIFIER SEMICOLON
    {
        auto v = b.make(ast::NodeKind::VarDecl, SL(@$));
        b.node(v).value = std::move($2);
        b.link(v, ast::EdgeRole::DeclType, $1);
        $$ = v;
    }
  | type IDENTIFIER ASSIGN expression SEMICOLON
    {
        auto v = b.make(ast::NodeKind::VarDecl, SL(@$));
        b.node(v).value = std::move($2);
        b.link(v, ast::EdgeRole::DeclType, $1);
        b.link(v, ast::EdgeRole::Init, $4);
        $$ = v;
    }
    ;

expr_stmt:
    expression SEMICOLON
    {
        auto s = b.make(ast::NodeKind::ExprStmt, SL(@$));
        b.link(s, ast::EdgeRole::Expr, $1);
        $$ = s;
    }
    ;

if_stmt:
    IF LPAREN expression RPAREN block elif_chain
    {
        auto n = b.make(ast::NodeKind::If, SL(@$));
        b.link(n, ast::EdgeRole::Cond, $3);
        b.link(n, ast::EdgeRole::Then, $5);
        if ($6) {
            b.link(n, ast::EdgeRole::ElifBranch, *$6);
        }
        $$ = n;
    }
    ;

elif_chain:
    /* пусто */  { $$ = std::nullopt; }
  | ELIF LPAREN expression RPAREN block elif_chain
    {
        auto n = b.make(ast::NodeKind::Elif, SL(@$));
        b.link(n, ast::EdgeRole::Cond, $3);
        b.link(n, ast::EdgeRole::Then, $5);
        if ($6) {
            b.link(n, ast::EdgeRole::ElifBranch, *$6);
        }
        $$ = n;
    }
  | ELSE block
    {
        auto n = b.make(ast::NodeKind::Else, SL(@$));
        b.link(n, ast::EdgeRole::Then, $2);
        $$ = n;
    }
    ;

while_stmt:
    WHILE LPAREN expression RPAREN block
    {
        auto n = b.make(ast::NodeKind::While, SL(@$));
        b.link(n, ast::EdgeRole::Cond, $3);
        b.link(n, ast::EdgeRole::LoopBody, $5);
        $$ = n;
    }
    ;

for_stmt:
    FOR LPAREN expression SEMICOLON
                expression SEMICOLON
                expression RPAREN block
    {
        auto n = b.make(ast::NodeKind::For, SL(@$));
        b.link(n, ast::EdgeRole::ForInit, $3);
        b.link(n, ast::EdgeRole::ForCond, $5);
        b.link(n, ast::EdgeRole::ForStep, $7);
        b.link(n, ast::EdgeRole::LoopBody, $9);
        $$ = n;
    }
    ;

return_stmt:
    RETURN SEMICOLON
    { $$ = b.make(ast::NodeKind::Return, SL(@$)); }
  | RETURN expression SEMICOLON
    {
        auto n = b.make(ast::NodeKind::Return, SL(@$));
        b.link(n, ast::EdgeRole::RetValue, $2);
        $$ = n;
    }
    ;

/* выражения */
expression:
    NUMBER          { $$ = b.number($1, SL(@1)); }
  | FLOAT           { $$ = b.floating($1, SL(@1)); }
  | CHAR_LITERAL    { $$ = b.ch($1, SL(@1)); }
  | STRING_LITERAL  { $$ = b.string(std::move($1), SL(@1)); }
  | TRUE_LIT        { $$ = b.boolean(true,  SL(@1)); }
  | FALSE_LIT       { $$ = b.boolean(false, SL(@1)); }
  | IDENTIFIER      { $$ = b.identifier(std::move($1), SL(@1)); }
  | LPAREN expression RPAREN  { $$ = $2; }

  | expression PLUS expression      { $$ = b.binary(ast::BinaryOp::Add, $1, $3, SL(@$)); }
  | expression MINUS expression     { $$ = b.binary(ast::BinaryOp::Sub, $1, $3, SL(@$)); }
  | expression STAR expression      { $$ = b.binary(ast::BinaryOp::Mul, $1, $3, SL(@$)); }
  | expression SLASH expression     { $$ = b.binary(ast::BinaryOp::Div, $1, $3, SL(@$)); }
  | expression PERCENT expression   { $$ = b.binary(ast::BinaryOp::Mod, $1, $3, SL(@$)); }

  | expression EQ expression        { $$ = b.binary(ast::BinaryOp::Eq, $1, $3, SL(@$)); }
  | expression NEQ expression       { $$ = b.binary(ast::BinaryOp::Ne, $1, $3, SL(@$)); }
  | expression LT expression        { $$ = b.binary(ast::BinaryOp::Lt, $1, $3, SL(@$)); }
  | expression GT expression        { $$ = b.binary(ast::BinaryOp::Gt, $1, $3, SL(@$)); }
  | expression LE expression        { $$ = b.binary(ast::BinaryOp::Le, $1, $3, SL(@$)); }
  | expression GE expression        { $$ = b.binary(ast::BinaryOp::Ge, $1, $3, SL(@$)); }

  | expression AMP expression       { $$ = b.binary(ast::BinaryOp::BitAnd, $1, $3, SL(@$)); }
  | expression PIPE expression      { $$ = b.binary(ast::BinaryOp::BitOr,  $1, $3, SL(@$)); }
  | expression CARET expression     { $$ = b.binary(ast::BinaryOp::BitXor, $1, $3, SL(@$)); }
  | expression LSHIFT expression    { $$ = b.binary(ast::BinaryOp::Shl,    $1, $3, SL(@$)); }
  | expression RSHIFT expression    { $$ = b.binary(ast::BinaryOp::Shr,    $1, $3, SL(@$)); }

  | expression AND expression       { $$ = b.binary(ast::BinaryOp::LogAnd, $1, $3, SL(@$)); }
  | expression OR  expression       { $$ = b.binary(ast::BinaryOp::LogOr,  $1, $3, SL(@$)); }

  | MINUS expression %prec UNARY_MINUS  { $$ = b.unary(ast::UnaryOp::Neg,    $2, SL(@$)); }
  | NOT   expression                    { $$ = b.unary(ast::UnaryOp::Not,    $2, SL(@$)); }
  | TILDE expression                    { $$ = b.unary(ast::UnaryOp::BitNot, $2, SL(@$)); }
  | AMP   expression %prec UNARY_AMP    { $$ = b.unary(ast::UnaryOp::Addr,   $2, SL(@$)); }
  | STAR  expression %prec UNARY_STAR   { $$ = b.unary(ast::UnaryOp::Deref,  $2, SL(@$)); }

  | expression ASSIGN expression          { $$ = b.assign(ast::AssignOp::Assign, $1, $3, SL(@$)); }
  | expression PLUS_ASSIGN expression     { $$ = b.assign(ast::AssignOp::Add,    $1, $3, SL(@$)); }
  | expression MINUS_ASSIGN expression    { $$ = b.assign(ast::AssignOp::Sub,    $1, $3, SL(@$)); }
  | expression STAR_ASSIGN expression     { $$ = b.assign(ast::AssignOp::Mul,    $1, $3, SL(@$)); }
  | expression SLASH_ASSIGN expression    { $$ = b.assign(ast::AssignOp::Div,    $1, $3, SL(@$)); }
  | expression PERCENT_ASSIGN expression  { $$ = b.assign(ast::AssignOp::Mod,    $1, $3, SL(@$)); }
  | expression AMP_ASSIGN expression      { $$ = b.assign(ast::AssignOp::BitAnd, $1, $3, SL(@$)); }
  | expression PIPE_ASSIGN expression     { $$ = b.assign(ast::AssignOp::BitOr,  $1, $3, SL(@$)); }
  | expression CARET_ASSIGN expression    { $$ = b.assign(ast::AssignOp::BitXor, $1, $3, SL(@$)); }
  | expression LSHIFT_ASSIGN expression   { $$ = b.assign(ast::AssignOp::Shl,    $1, $3, SL(@$)); }
  | expression RSHIFT_ASSIGN expression   { $$ = b.assign(ast::AssignOp::Shr,    $1, $3, SL(@$)); }

  | expression LPAREN arg_list RPAREN
    {
        auto n = b.make(ast::NodeKind::Call, SL(@$));
        b.link(n, ast::EdgeRole::Callee, $1);
        for (auto a : $3) b.link(n, ast::EdgeRole::Arg, a);
        $$ = n;
    }
  | expression LBRACKET expression RBRACKET
    {
        auto n = b.make(ast::NodeKind::Index, SL(@$));
        b.link(n, ast::EdgeRole::Base, $1);
        b.link(n, ast::EdgeRole::Subscript, $3);
        $$ = n;
    }
    ;

arg_list:
    /* пусто */             { $$ = {}; }
  | expression              { $$ = { $1 }; }
  | arg_list COMMA expression
    { $1.push_back($3); $$ = std::move($1); }
    ;

%%

void yy::Parser::error(const location_type& loc, const std::string& msg) {
    throw err::CompileError(
        err::Stage::Syntax,
        ast::SourceLocation{loc.begin.line, loc.begin.column},
        msg
    );
}