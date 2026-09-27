#pragma once

#include "source_location.hpp"

#include <stdexcept>
#include <string>

namespace err {
	// стадия на которой произошла ошибка
	enum class Stage { Lexical, Syntax, Semantic };

	// строковое имя стадии для вывода в квадратных скобках
	inline const char* stage_name(Stage s) noexcept {
		switch (s) {
			case Stage::Lexical:  return "lexical";
			case Stage::Syntax:   return "syntax";
			case Stage::Semantic: return "semantic";
		}
		return "?";
	}

	// исключение, которое бросают лексер, парсер и семантический анализатор
	// несёт стадию, позицию в исходнике и текстовое сообщение (через what())
	class CompileError : public std::runtime_error {
	public:
		CompileError(Stage stage, ast::SourceLocation loc, std::string msg)
				: std::runtime_error(std::move(msg)), stage_(stage), loc_(loc) {}

		[[nodiscard]] Stage stage() const noexcept { return stage_; }
		[[nodiscard]] ast::SourceLocation location() const noexcept { return loc_; }

	private:
		Stage stage_;
		ast::SourceLocation loc_;
	};

} // namespace err