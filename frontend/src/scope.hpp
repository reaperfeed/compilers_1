#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include "source_location.hpp"

namespace sema {

// один именованный объект в области видимости
// хранит имя и что это было
	struct Symbol {
		std::string name;
		enum class Kind { Variable, Parameter, Function } kind = Kind::Variable;
		ast::SourceLocation loc;    // для определения места ошибки
	};

// один лексический scope, ссылается на родителя для поиска
	class Scope { // область видимости
	public:
		explicit Scope(Scope* parent) noexcept : parent_(parent) {}

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

		// возвращает false, если имя уже объявлено в этом же scope
		bool declare(Symbol sym) {
			auto [_, inserted] = symbols_.emplace(sym.name, std::move(sym));
			return inserted;
		}

		// ищет имя в этом scope и во всех вложенных родителях
		const Symbol* lookup(const std::string& name) const noexcept {
			for (const Scope* s = this; s != nullptr; s = s->parent_) {
				auto it = s->symbols_.find(name);
				if (it != s->symbols_.end()) return &it->second;
			}
			return nullptr;
		}

		// ищет только в текущем scope (нужно для проверки дубликатов)
		const Symbol* lookup_local(const std::string& name) const noexcept {
			auto it = symbols_.find(name);
			return it == symbols_.end() ? nullptr : &it->second;
		}

		Scope* parent() const noexcept { return parent_; }

	private:
		Scope* parent_;
		std::unordered_map<std::string, Symbol> symbols_;
	};

// стек скоупов
// элементы хранятся в unique_ptr, чтобы адрес Scope не менялся при push/pop — тогда parent_ остаётся валидным.
	class ScopeStack {
	public:
		ScopeStack() {
			scopes_.push_back(std::make_unique<Scope>(nullptr));
		}

		Scope& current() noexcept { return *scopes_.back(); }
		[[nodiscard]] const Scope& current() const noexcept { return *scopes_.back(); }

		void push() {
			scopes_.push_back(std::make_unique<Scope>(scopes_.back().get()));
		}

		void pop() {
			// Глобальный scope (нулевой) не удаляем.
			if (scopes_.size() > 1) scopes_.pop_back();
		}

		[[nodiscard]] std::size_t depth() const noexcept { return scopes_.size(); }

	private:
		std::vector<std::unique_ptr<Scope>> scopes_;
	};

// RAII: открывает scope на время жизни объекта.
	class ScopeGuard {
	public:
		explicit ScopeGuard(ScopeStack& stack) noexcept : stack_(stack) {
			stack_.push();
		}
		~ScopeGuard() { stack_.pop(); }

		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard& operator=(const ScopeGuard&) = delete;

	private:
		ScopeStack& stack_;
	};

} // namespace sema