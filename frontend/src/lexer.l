%{
#include "parser.hpp"
#include "errors.hpp"

#include <cstdio>
#include <string>

//текущая позиция для Bison
static yy::Parser::location_type yylloc;

//номер строки для диагностики
static int current_line = 1;

//переопределяем на настоящий EOF-токен Bison
#define yyterminate() return yy::Parser::make_END(yylloc)

#define YY_USER_ACTION                              \
    do {                                            \
        yylloc.begin.line   = current_line;         \
        yylloc.end.line     = current_line;         \
        yylloc.begin.column = yylloc.end.column;    \
        yylloc.end.column  += yyleng;               \
    } while (0);

//переопределяем сигнатуру yylex: Bison C++ ожидает symbol_type
#define YY_DECL yy::Parser::symbol_type yylex()
%}

%option noyywrap

DIGIT   [0-9]
LETTER  [a-zA-Z_]
ID      {LETTER}({LETTER}|{DIGIT})*
INT     {DIGIT}+

%%

[ \t\r]+        { /* пробелы и возврат каретки игнорируем */ }

\n              {
                    current_line++;
                    yylloc.end.column   = 0;
                    yylloc.begin.column = 0;
                }

"//".*          { /* однострочный комментарий */ }

"/*"([^*]|\*+[^*/])*\*+"/"  { /* многострочный комментарий */ }

"if"            { return yy::Parser::make_IF(yylloc); }
"elif"          { return yy::Parser::make_ELIF(yylloc); }
"else"          { return yy::Parser::make_ELSE(yylloc); }
"while"         { return yy::Parser::make_WHILE(yylloc); }
"for"           { return yy::Parser::make_FOR(yylloc); }
"return"        { return yy::Parser::make_RETURN(yylloc); }
"i32"           { return yy::Parser::make_I32(yylloc); }
"bool"          { return yy::Parser::make_BOOL(yylloc); }
"char"          { return yy::Parser::make_CHAR(yylloc); }
"ptr"           { return yy::Parser::make_PTR(yylloc); }
"true"          { return yy::Parser::make_TRUE_LIT(yylloc); }
"false"         { return yy::Parser::make_FALSE_LIT(yylloc); }

{ID}            { return yy::Parser::make_IDENTIFIER(std::string(yytext), yylloc); }
{INT}           { return yy::Parser::make_NUMBER(std::stoll(yytext), yylloc); }

\'([^'\\]|\\.)\'    {
                        char value = 0;
                        if (yyleng == 3) {
                            value = yytext[1];
                        } else if (yyleng == 4 && yytext[1] == '\\') {
                            switch (yytext[2]) {
                                case 'n':  value = '\n'; break;
                                case 't':  value = '\t'; break;
                                case '\\': value = '\\'; break;
                                case '\'': value = '\''; break;
                                case '0':  value = '\0'; break;
                                default:   value = yytext[2]; break;
                            }
                        }
                        return yy::Parser::make_CHAR_LITERAL(value, yylloc);
                    }

\"([^\"\\]|\\.)*\"  {
                        std::string raw(yytext + 1, yyleng - 2);
                        std::string out;
                        out.reserve(raw.size());
                        for (std::size_t i = 0; i < raw.size(); ++i) {
                            char c = raw[i];
                            if (c == '\\' && i + 1 < raw.size()) {
                                char n = raw[++i];
                                switch (n) {
                                    case 'n':  out += '\n'; break;
                                    case 't':  out += '\t'; break;
                                    case 'r':  out += '\r'; break;
                                    case '\\': out += '\\'; break;
                                    case '"':  out += '"';  break;
                                    case '\'': out += '\''; break;
                                    case '0':  out += '\0'; break;
                                    default:   out += n;    break;
                                }
                            } else {
                                out += c;
                            }
                        }
                        return yy::Parser::make_STRING_LITERAL(std::move(out), yylloc);
                    }

"=="            { return yy::Parser::make_EQ(yylloc); }
"!="            { return yy::Parser::make_NEQ(yylloc); }
"<="            { return yy::Parser::make_LE(yylloc); }
">="            { return yy::Parser::make_GE(yylloc); }
"<<"            { return yy::Parser::make_LSHIFT(yylloc); }
">>"            { return yy::Parser::make_RSHIFT(yylloc); }
"&&"            { return yy::Parser::make_AND(yylloc); }
"||"            { return yy::Parser::make_OR(yylloc); }
"+="            { return yy::Parser::make_PLUS_ASSIGN(yylloc); }
"-="            { return yy::Parser::make_MINUS_ASSIGN(yylloc); }
"*="            { return yy::Parser::make_STAR_ASSIGN(yylloc); }
"/="            { return yy::Parser::make_SLASH_ASSIGN(yylloc); }
"%="            { return yy::Parser::make_PERCENT_ASSIGN(yylloc); }
"&="            { return yy::Parser::make_AMP_ASSIGN(yylloc); }
"|="            { return yy::Parser::make_PIPE_ASSIGN(yylloc); }
"^="            { return yy::Parser::make_CARET_ASSIGN(yylloc); }
"<<="           { return yy::Parser::make_LSHIFT_ASSIGN(yylloc); }
">>="           { return yy::Parser::make_RSHIFT_ASSIGN(yylloc); }

"+"             { return yy::Parser::make_PLUS(yylloc); }
"-"             { return yy::Parser::make_MINUS(yylloc); }
"*"             { return yy::Parser::make_STAR(yylloc); }
"/"             { return yy::Parser::make_SLASH(yylloc); }
"%"             { return yy::Parser::make_PERCENT(yylloc); }
"<"             { return yy::Parser::make_LT(yylloc); }
">"             { return yy::Parser::make_GT(yylloc); }
"&"             { return yy::Parser::make_AMP(yylloc); }
"|"             { return yy::Parser::make_PIPE(yylloc); }
"^"             { return yy::Parser::make_CARET(yylloc); }
"~"             { return yy::Parser::make_TILDE(yylloc); }
"!"             { return yy::Parser::make_NOT(yylloc); }
"="             { return yy::Parser::make_ASSIGN(yylloc); }

"("             { return yy::Parser::make_LPAREN(yylloc); }
")"             { return yy::Parser::make_RPAREN(yylloc); }
"{"             { return yy::Parser::make_LBRACE(yylloc); }
"}"             { return yy::Parser::make_RBRACE(yylloc); }
"["             { return yy::Parser::make_LBRACKET(yylloc); }
"]"             { return yy::Parser::make_RBRACKET(yylloc); }
";"             { return yy::Parser::make_SEMICOLON(yylloc); }
","             { return yy::Parser::make_COMMA(yylloc); }

.               {
                    throw err::CompileError(
                        err::Stage::Lexical,
                        ast::SourceLocation{
                            yylloc.begin.line,
                            yylloc.begin.column
                        },
                        std::string("unexpected character '") + yytext + "'"
                    );
                }

%%