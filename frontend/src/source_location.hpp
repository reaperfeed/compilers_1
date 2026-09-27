#pragma once

namespace ast {
	// место где возникла ошибка
	struct SourceLocation {
		int line   = 0;
		int column = 0;
	};

} // namespace ast