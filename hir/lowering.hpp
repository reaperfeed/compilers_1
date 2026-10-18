#pragma once
#include "ast.hpp"
#include "hir.hpp"

namespace hir {
// A depth-first AST translator. All operands are evaluated before the operation
// that consumes them; implicit i32 -> f32 coercions become explicit converts.
TranslationUnit lower(const ast::AstTree& tree);
}
