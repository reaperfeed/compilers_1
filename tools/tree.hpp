#pragma once

#include "graph.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tree {

	template <Copyable Tnode_data, Copyable Tedge_data>
	class tree_t : public graph::orgraph_t<Tnode_data, Tedge_data> {
		using base_t = graph::orgraph_t<Tnode_data, Tedge_data>;

		using node_type =
				typename std::iterator_traits<
						typename base_t::const_node_iterator
				>::value_type;

	public:
		//даем доступ к итераторам базового класса
		using typename base_t::const_node_iterator;
		using typename base_t::node_iterator;

		node_iterator root() noexcept {
			return find_root(this->begin(), this->end());
		}

		const_node_iterator root() const noexcept {
			return find_root(this->begin(), this->end());
		}

		//проверка, является ли дерево корректным
		bool check() const override {
			if (!base_t::check()) {
				return false;
			}

			if (this->begin() == this->end()) {
				return true;
			}
			const auto tree_root = root();
			//если root=end, то либо корня нет, либо их несколько
			if (tree_root == this->end()) {
				return false;
			}
			//у любой вершины дерева ровно одно входящее ребро
			for (auto node = this->begin(); node != this->end(); ++node) {
				if (node->in_begin() != node->in_end()) {
					auto incoming = node->in_begin();
					++incoming;

					//если получили 2 входящих ребра -> не дерево
					if (incoming != node->in_end()) {
						return false;
					}
				}
			}

			//через обход в глубину определяем, что все вершины достижимы из корня дерева
			std::unordered_set<const node_type*> visited;
			std::vector<const_node_iterator> stack;

			stack.push_back(tree_root);

			while (!stack.empty()) {
				const auto current = stack.back();
				stack.pop_back();
				const auto* current_ptr = std::addressof(*current);
				if (!visited.insert(current_ptr).second) {
					return false;
				}

				for (auto edge = current->out_begin();
				     edge != current->out_end();
				     ++edge) {
					const auto next = std::as_const(**edge).next_node();
					stack.push_back(next);
				}
			}

			std::size_t visited_count = visited.size();
			std::size_t total_count = 0;

			for (auto node = this->begin();
			     node != this->end();
			     ++node) {
				++total_count;
			}

			return visited_count == total_count;
		}

	private:
		template <typename Titerator>
		//ищет корень между итераторами first и last
		//в случае ошибки возвращается last
		static Titerator find_root(Titerator first, Titerator last) noexcept {
			const auto root_it = std::find_if(
					first,
					last,
					[](const auto& node) {
						return node.in_begin() == node.in_end();
					}
			);

			if (root_it == last) {
				return last;
			}

			const auto another_root = std::find_if(
					std::next(root_it),
					last,
					[](const auto& node) {
						return node.in_begin() == node.in_end();
					}
			);

			return another_root == last ? root_it : last;
		}
	};

} // namespace tree