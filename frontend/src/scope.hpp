#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include "source_location.hpp"

namespace sema {
	enum class Type {
		Int,
		Float,
		Bool,
		Char,
		String,
		Void,
		Unknown
	};

	inline std::string type_to_string(Type t) {
		switch (t) {
			case Type::Int:    return "i32";
			case Type::Float:  return "f32";
			case Type::Bool:   return "bool";
			case Type::Char:   return "char";
			case Type::String: return "string";
			case Type::Void:   return "void";
			default:           return "unknown";
		}
	}

	inline bool is_assignable(Type target, Type source) {
		if (target == source) return true;
		// int можно безопасно привести к float
		if (target == Type::Float && source == Type::Int) return true;
		return false;
	}

// один именованный объект в области видимости
// хранит имя и что это было
	struct Symbol {
		std::string name;
		enum class Kind { Variable, Parameter, Function } kind = Kind::Variable;
		Type type = Type::Unknown;               // тип переменной/возврата функции
		std::vector<Type> param_types;           // сигнатура функции
		bool is_variadic = false;                // для printf
		ast::SourceLocation loc;                 // локация для ошибок
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

		Symbol* lookup_mutable(const std::string& name) noexcept {
			for (Scope* s = this; s != nullptr; s = s->parent_) {
				auto it = s->symbols_.find(name);
				if (it != s->symbols_.end()) return &it->second;
			}
			return nullptr;
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