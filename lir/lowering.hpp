#pragma once

#include "hir.hpp"
#include "lir.hpp"

namespace lir {

// Assigns HIR objects to virtual registers or bp-relative stack slots, then
// expands memory and control operations into the educational machine IR.
[[nodiscard]] TranslationUnit lower(const hir::TranslationUnit& unit);

} // namespace lir
