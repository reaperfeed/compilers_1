#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <stdexcept>

#ifdef DEBUG

#define GRAPH_ASSERT(condition, message)    \
    do {                                    \
        if (!(condition)) {                 \
            throw std::logic_error(message);\
        }                                   \
    } while (false)

#else

#define GRAPH_ASSERT(condition, message)    \
    do {                                    \
    } while (false)

#endif

template <typename T>
concept Copyable = std::is_void_v<T> || (std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>);

namespace graph {
namespace detail {
struct empty_data {};

template <typename T> using data_storage_t = std::conditional_t<std::is_void_v<T>, empty_data, T>;

template <typename T, typename... Targs>
concept DataConstructible =
    (std::is_void_v<T> && sizeof...(Targs) == 0) || (!std::is_void_v<T> && std::is_constructible_v<T, Targs...>);
} // namespace detail

template <Copyable Tnode_data, Copyable Tedge_data> class orgraph_t {
  public:
    class node_t;
    class edge_t;

    using node_iterator = std::list<node_t>::iterator;
    using edge_iterator = std::list<edge_t>::iterator;
    using const_node_iterator = typename std::list<node_t>::const_iterator;
    using const_edge_iterator = typename std::list<edge_t>::const_iterator;

    class edge_t {
        friend class orgraph_t;
        using data_type = detail::data_storage_t<Tedge_data>;

      private:
        [[no_unique_address]] data_type edge_data;      //данные ребра
        node_iterator prev;                             //вершина, из которой выходит ребро
        node_iterator next;                             //вершины, куда входит ребро

      public:
        edge_t(data_type data_, node_iterator prev_, node_iterator next_)
            requires(!std::is_void_v<Tedge_data>)
            : edge_data(data_), prev(prev_), next(next_) {}

        template <typename... Targs>
            requires detail::DataConstructible<Tedge_data, Targs...>
        edge_t(std::in_place_t, node_iterator prev_, node_iterator next_, Targs &&...args)
            : edge_data(std::forward<Targs>(args)...), prev(prev_), next(next_) {}

        node_iterator &prev_node() noexcept { return prev; }
        node_iterator &next_node() noexcept { return next; }

        const_node_iterator prev_node() const noexcept { return prev; }
        const_node_iterator next_node() const noexcept { return next; }

        data_type &data() noexcept
            requires(!std::is_void_v<Tedge_data>)
        {
            return edge_data;
        }
        const data_type &data() const noexcept
            requires(!std::is_void_v<Tedge_data>)
        {
            return edge_data;
        }
    };

    class node_t {
        friend class orgraph_t;
        using data_type = detail::data_storage_t<Tnode_data>;

        [[no_unique_address]] data_type node_data;      //данные вершины
        std::list<edge_iterator> in;                    //список входящих ребер
        std::list<edge_iterator> out;                   //список исходящих ребер

      public:
        node_t(data_type data_)
            requires(!std::is_void_v<Tnode_data>)
            : node_data(data_) {}

        template <typename... Targs>
            requires detail::DataConstructible<Tnode_data, Targs...>
        node_t(std::in_place_t, Targs &&...args) : node_data(std::forward<Targs>(args)...) {}

        auto in_begin() noexcept { return in.begin(); }
        const auto in_begin() const noexcept { return in.begin(); }

        auto in_end() noexcept { return in.end(); }
        const auto in_end() const noexcept { return in.end(); }

        auto out_begin() noexcept { return out.begin(); }
        const auto out_begin() const noexcept { return out.begin(); }

        auto out_end() noexcept { return out.end(); }
        const auto out_end() const noexcept { return out.end(); }

        data_type &data() noexcept
            requires(!std::is_void_v<Tnode_data>)
        {
            return node_data;
        }
        const data_type &data() const noexcept
            requires(!std::is_void_v<Tnode_data>)
        {
            return node_data;
        }

        void push_in_edge(edge_iterator edge) { in.push_back(edge); }
        void push_out_edge(edge_iterator edge) { out.push_back(edge); }
    };

  private:
    std::list<node_t> nodes;    //вершины
    std::list<edge_t> edges;    //ребра

  public:
    orgraph_t() = default;

    orgraph_t(const orgraph_t &other) {
        std::unordered_map<const node_t *, node_iterator> node_map;
        std::unordered_map<const edge_t *, edge_iterator> edge_map;

        for (const auto &node : other.nodes) {
            if constexpr (std::is_void_v<Tnode_data>) {
                node_map.emplace(std::addressof(node), new_node());
            } else {
                node_map.emplace(std::addressof(node), new_node(node.data()));
            }
        }
        for (const auto &edge : other.edges) {
            const auto prev = node_map.at(std::addressof(*edge.prev));
            const auto next = node_map.at(std::addressof(*edge.next));
            if constexpr (std::is_void_v<Tedge_data>) {
                edge_map.emplace(std::addressof(edge), edges.emplace(edges.end(), std::in_place, prev, next));
            } else {
                edge_map.emplace(std::addressof(edge),
                                 edges.emplace(edges.end(), std::in_place, prev, next, edge.data()));
            }
        }
        for (const auto &node : other.nodes) {
            auto copy = node_map.at(std::addressof(node));
            for (auto edge : node.in) {
                copy->push_in_edge(edge_map.at(std::addressof(*edge)));
            }
            for (auto edge : node.out) {
                copy->push_out_edge(edge_map.at(std::addressof(*edge)));
            }
        }
    }

    orgraph_t &operator=(const orgraph_t &other) {
        if (this != std::addressof(other)) {
            orgraph_t copy(other);
            swap(copy);
        }
        return *this;
    }

    orgraph_t(orgraph_t &&other) noexcept { swap(other); }

    orgraph_t &operator=(orgraph_t &&other) noexcept {
        if (this != std::addressof(other)) {
            orgraph_t moved(std::move(other));
            swap(moved);
        }
        return *this;
    }

    virtual ~orgraph_t() = default;

    void swap(orgraph_t &other) noexcept {
        nodes.swap(other.nodes);
        edges.swap(other.edges);
    }

    friend void swap(orgraph_t &left, orgraph_t &right) noexcept { left.swap(right); }

  public:
    auto begin() noexcept { return nodes.begin(); }
    const auto begin() const noexcept { return nodes.begin(); }

    auto end() noexcept { return nodes.end(); }
    const auto end() const noexcept { return nodes.end(); }

    node_iterator new_node(const detail::data_storage_t<Tnode_data> &data)
        requires(!std::is_void_v<Tnode_data>)
    {
        return nodes.emplace(nodes.end(), std::in_place, data);
    }

    template <typename... Targs>
        requires detail::DataConstructible<Tnode_data, Targs...>
    node_iterator new_node(Targs &&...args) {
	    auto node = nodes.emplace(
			    nodes.end(),
			    std::in_place,
			    std::forward<Targs>(args)...
	    );
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
        return node;
    }

    edge_iterator new_edge(detail::data_storage_t<Tedge_data> data_, node_iterator prev_, node_iterator next_)
        requires(!std::is_void_v<Tedge_data>)
    {
		auto edge = new_edge_impl(prev_, next_, data_);
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
        return edge;
    }

    template <typename... Targs>
        requires detail::DataConstructible<Tedge_data, Targs...>
    edge_iterator new_edge(node_iterator prev_, node_iterator next_, Targs &&...args) {
		auto edge = new_edge_impl(prev_, next_, std::forward<Targs>(args)...);
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
        return edge;
    }

    void set_prev_node(edge_iterator edge, node_iterator prev_node) {
        if (edge->prev == prev_node) {
            return;
        }
        auto &old_out = edge->prev->out;
        const auto position = std::find(old_out.begin(), old_out.end(), edge);
        if (position == old_out.end()) {
            return;
        }
        prev_node->out.splice(prev_node->out.end(), old_out, position);
        edge->prev = prev_node;

	    GRAPH_ASSERT(this->check(), "Graph is invalid");
    }

    void set_next_node(edge_iterator edge, node_iterator next_node) {
        if (edge->next == next_node) {
            return;
        }
        auto &old_in = edge->next->in;
        const auto position = std::find(old_in.begin(), old_in.end(), edge);
        if (position == old_in.end()) {
            return;
        }
        next_node->in.splice(next_node->in.end(), old_in, position);
        edge->next = next_node;
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
    }

    void erase_edge(edge_iterator edge) {
        if (!has_unique_edge_links(edge)) {
            return;
        }
        auto &out = edge->prev->out;
        auto &in = edge->next->in;
        const auto out_position = std::find(out.begin(), out.end(), edge);
        const auto in_position = std::find(in.begin(), in.end(), edge);
        out.erase(out_position);
        in.erase(in_position);
        edges.erase(edge);
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
    }

    void erase_node(node_iterator node) {
        std::size_t incoming = 0;
        std::size_t outgoing = 0;
        for (auto edge = edges.begin(); edge != edges.end(); ++edge) {
            const bool enters = edge->next == node;
            const bool leaves = edge->prev == node;
            if (enters || leaves) {
                if (!has_unique_edge_links(edge)) {
                    return;
                }
                incoming += enters;
                outgoing += leaves;
            }
        }
        if (incoming != node->in.size() || outgoing != node->out.size()) {
            return;
        }

        while (!node->in.empty()) {
            erase_edge(node->in.front());
        }
        while (!node->out.empty()) {
            erase_edge(node->out.front());
        }
        nodes.erase(node);
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
    }

	virtual bool check() const {
		//для каждого ребра запоминаем сколько раз оно встречается в списках in и out.
		struct edge_usage {
			std::size_t in_count = 0;
			std::size_t out_count = 0;
		};

		std::unordered_map<const edge_t*, edge_usage> usage_stats;
		//проверяем все ребра на существование обоих вершин
		for (const auto& e : edges) {
			if (e.prev == nodes.end() || e.next == nodes.end()) return false;
			usage_stats[std::addressof(e)] = {};
		}

		//проверяем все вершины на существование всех ребер
		for (const auto& n : nodes) {
			for (auto it = n.in.begin(); it != n.in.end(); ++it) {
				if (*it == edges.end()) return false;
				const edge_t* e_ptr = std::addressof(**it);
				if (!usage_stats.contains(e_ptr)) return false;
				if (std::addressof(*((*it)->next)) != std::addressof(n)) return false;
				if (++usage_stats[e_ptr].in_count > 1) return false;
			}

			for (auto it = n.out.begin(); it != n.out.end(); ++it) {
				if (*it == edges.end()) return false;

				const edge_t* e_ptr = std::addressof(**it);
				if (!usage_stats.contains(e_ptr)) return false;
				if (std::addressof(*((*it)->prev)) != std::addressof(n)) return false;
				if (++usage_stats[e_ptr].out_count > 1) return false;
			}
		}

		for (const auto& [ptr, stats] : usage_stats) {
			if (stats.in_count != 1 || stats.out_count != 1) return false;
		}

		return true;
	}

	template <typename Tnode_label, typename Tedge_label>
	void write_dot(std::ostream& output,
	               Tnode_label node_label,
	               Tedge_label edge_label) const
	{
		if (!check()) {
			throw std::logic_error("Graph validation failed before export");
		}

		auto quote = [](std::string_view text) -> std::string {
			std::string result;
			result.reserve(text.size() + 2);

			result += '"';

			for (char c : text) {
				switch (c) {
					case '\\': result += "\\\\"; break;
					case '"':  result += "\\\""; break;
					case '\n': result += "\\n";  break;
					case '\r': result += "\\r";  break;
					case '\t': result += "\\t";  break;
					default:   result += c;      break;
				}
			}

			result += '"';
			return result;
		};

		output << "digraph G {\n";

		//для каждого узла создаём уникальный идентификатор
		std::unordered_map<const node_t*, std::string> node_ids;

		std::size_t id = 0;
		for (const auto& node : nodes) {
			node_ids.emplace(
					std::addressof(node),
					std::to_string(id++)
			);
		}

		//вывод узлов
		for (const auto& node : nodes) {
			std::string label;

			if constexpr (std::is_void_v<Tnode_data>) {
				label = std::invoke(node_label);
			} else {
				label = std::invoke(node_label, node.data());
			}

			output << "    "
			       << node_ids.at(std::addressof(node))
			       << " [label=" << quote(label) << "];\n";
		}

		//вывод дуг
		for (const auto& edge : edges) {
			std::string label;

			if constexpr (std::is_void_v<Tedge_data>) {
				label = std::invoke(edge_label);
			} else {
				label = std::invoke(edge_label, edge.data());
			}

			const auto& from =
					node_ids.at(std::addressof(*edge.prev));

			const auto& to =
					node_ids.at(std::addressof(*edge.next));

			output << "    "
			       << from << " -> " << to
			       << " [label=" << quote(label) << "];\n";
		}

		output << "}\n";

		if (!output) {
			throw std::runtime_error("Failed to write stream");
		}
	}

  private:
    bool has_unique_edge_links(edge_iterator edge) const noexcept {
        const auto &out = edge->prev->out;
        const auto &in = edge->next->in;
        return std::count(out.begin(), out.end(), edge) == 1 && std::count(in.begin(), in.end(), edge) == 1;
    }

    template <typename... Targs>
    edge_iterator new_edge_impl(node_iterator prev_, node_iterator next_, Targs &&...args) {
        auto edge = edges.emplace(edges.end(), std::in_place, prev_, next_, std::forward<Targs>(args)...);
        try {
            prev_->push_out_edge(edge);
        } catch (...) {
            edges.erase(edge);
            throw;
        }
        try {
            next_->push_in_edge(edge);
        } catch (...) {
            prev_->out.pop_back();
            edges.erase(edge);
            throw;
        }
	    GRAPH_ASSERT(this->check(), "Graph is invalid");
        return edge;
    }
};
} // namespace graph