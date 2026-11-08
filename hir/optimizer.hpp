#pragma once
#include "hir.hpp"

namespace hir {
// Local constant propagation, integer simplification and dead code elimination.
// Requires a valid CFG; rebuilds and verifies it after optimization.
void optimize_local(TranslationUnit& unit);
}
