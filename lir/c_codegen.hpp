#pragma once

#include "lir.hpp"

namespace lir {

// Emit executable C using runtime/vm.h. The generated code has no native
// arguments or C locals: virtual registers are spilled into each VM frame.
// All program literals, including frame sizes and offsets, are global objects.
void emit_c(const TranslationUnit& unit, std::ostream& out);

} // namespace lir
