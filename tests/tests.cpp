#include <catch2/catch_test_macros.hpp>

#include "graph.hpp"
#include "tree.hpp"

#include <iterator>

TEST_CASE("Empty graph is valid") {
	graph::orgraph_t<int, int> graph;

	REQUIRE(graph.check());
	REQUIRE(graph.begin() == graph.end());
}

TEST_CASE("Graph with one edge") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);

	auto edge = graph.new_edge(a, b, 100);

	REQUIRE(graph.check());

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 1);
	REQUIRE(std::distance(b->in_begin(), b->in_end()) == 1);

	REQUIRE(edge->prev_node() == a);
	REQUIRE(edge->next_node() == b);

	REQUIRE(edge->data() == 100);
}

TEST_CASE("Graph with several edges") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	graph.new_edge(a, b, 10);
	graph.new_edge(a, c, 20);

	REQUIRE(graph.check());

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 2);
	REQUIRE(std::distance(b->in_begin(), b->in_end()) == 1);
	REQUIRE(std::distance(c->in_begin(), c->in_end()) == 1);
}

TEST_CASE("Erase edge") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);

	auto edge = graph.new_edge(a, b, 10);

	REQUIRE(graph.check());

	graph.erase_edge(edge);

	REQUIRE(graph.check());

	REQUIRE(a->out_begin() == a->out_end());
	REQUIRE(b->in_begin() == b->in_end());
}

TEST_CASE("Erase node with connected edges") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	graph.new_edge(a, b, 10);
	graph.new_edge(b, c, 20);

	REQUIRE(graph.check());

	graph.erase_node(b);

	REQUIRE(graph.check());
	REQUIRE(std::distance(graph.begin(), graph.end()) == 2);

	REQUIRE(a->out_begin() == a->out_end());
	REQUIRE(c->in_begin() == c->in_end());
}

TEST_CASE("Copy graph") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);

	graph.new_edge(a, b, 12);

	graph::orgraph_t<int, int> copy(graph);

	REQUIRE(copy.check());
	REQUIRE(std::distance(copy.begin(), copy.end()) == 2);

	auto copied_a = copy.begin();
	auto copied_b = std::next(copied_a);

	REQUIRE(copied_a->data() == 1);
	REQUIRE(copied_b->data() == 2);

	REQUIRE(std::distance(copied_a->out_begin(),
	                      copied_a->out_end()) == 1);

	auto copied_edge = copied_a->out_begin();

	REQUIRE((*copied_edge)->data() == 12);
}

TEST_CASE("Empty tree is valid") {
	tree::tree_t<int, int> tree;

	REQUIRE(tree.check());
	REQUIRE(tree.root() == tree.end());
}

TEST_CASE("Tree with one node") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);

	REQUIRE(tree.check());
	REQUIRE(tree.root() == root);
}

TEST_CASE("Simple tree") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);
	auto left = tree.new_node(2);
	auto right = tree.new_node(3);

	tree.new_edge(root, left, 10);
	tree.new_edge(root, right, 20);

	REQUIRE(tree.check());

	REQUIRE(tree.root() == root);

	REQUIRE(std::distance(root->out_begin(),
	                      root->out_end()) == 2);

	REQUIRE(std::distance(left->in_begin(),
	                      left->in_end()) == 1);

	REQUIRE(std::distance(right->in_begin(),
	                      right->in_end()) == 1);
}

TEST_CASE("Tree with several levels") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);
	auto a = tree.new_node(2);
	auto b = tree.new_node(3);
	auto c = tree.new_node(4);

	tree.new_edge(root, a, 10);
	tree.new_edge(root, b, 20);
	tree.new_edge(a, c, 30);

	REQUIRE(tree.check());
	REQUIRE(tree.root() == root);
}

TEST_CASE("Tree has no root when there are two roots") {
	tree::tree_t<int, int> tree;

	auto a = tree.new_node(1);
	auto b = tree.new_node(2);

	REQUIRE_FALSE(tree.check());
	REQUIRE(tree.root() == tree.end());
}

TEST_CASE("Tree is invalid when a node has two incoming edges") {
	tree::tree_t<int, int> tree;

	auto root1 = tree.new_node(1);
	auto root2 = tree.new_node(2);
	auto child = tree.new_node(3);

	tree.new_edge(root1, child, 10);
	tree.new_edge(root2, child, 20);

	REQUIRE_FALSE(tree.check());
}

TEST_CASE("Tree is invalid when not all nodes are reachable") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);
	auto child = tree.new_node(2);
	auto isolated = tree.new_node(3);

	tree.new_edge(root, child, 10);

	REQUIRE_FALSE(tree.check());
}

TEST_CASE("Tree can be modified") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);
	auto child = tree.new_node(2);

	auto edge = tree.new_edge(root, child, 100);

	REQUIRE(tree.check());
	REQUIRE(tree.root() == root);

	tree.erase_edge(edge);

	REQUIRE_FALSE(tree.check());
	REQUIRE(tree.root() == tree.end());
}

TEST_CASE("Tree copy") {
	tree::tree_t<int, int> tree;

	auto root = tree.new_node(1);
	auto child1 = tree.new_node(2);
	auto child2 = tree.new_node(3);

	tree.new_edge(root, child1, 10);
	tree.new_edge(root, child2, 20);

	tree::tree_t<int, int> copy(tree);

	REQUIRE(copy.check());
	REQUIRE(std::distance(copy.begin(), copy.end()) == 3);

	auto copied_root = copy.root();

	REQUIRE(copied_root != copy.end());
	REQUIRE(copied_root->data() == 1);
}

TEST_CASE("Change previous node of edge") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	auto edge = graph.new_edge(a, b, 12);

	REQUIRE(graph.check());

	graph.set_prev_node(edge, c);

	REQUIRE(graph.check());

	REQUIRE(edge->prev_node() == c);
	REQUIRE(edge->next_node() == b);

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 0);
	REQUIRE(std::distance(c->out_begin(), c->out_end()) == 1);
	REQUIRE(std::distance(b->in_begin(), b->in_end()) == 1);

	REQUIRE(edge->data() == 12);
}


TEST_CASE("Change next node of edge") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	auto edge = graph.new_edge(a, b, 12);

	REQUIRE(graph.check());

	graph.set_next_node(edge, c);

	REQUIRE(graph.check());

	REQUIRE(edge->prev_node() == a);
	REQUIRE(edge->next_node() == c);

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 1);
	REQUIRE(std::distance(b->in_begin(), b->in_end()) == 0);
	REQUIRE(std::distance(c->in_begin(), c->in_end()) == 1);

	REQUIRE(edge->data() == 12);
}


TEST_CASE("Change both endpoints of edge") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);
	auto d = graph.new_node(4);

	auto edge = graph.new_edge(a, b, 12);

	graph.set_prev_node(edge, c);
	graph.set_next_node(edge, d);

	REQUIRE(graph.check());

	REQUIRE(edge->prev_node() == c);
	REQUIRE(edge->next_node() == d);
	REQUIRE(edge->data() == 12);

	REQUIRE(c->out_begin() != c->out_end());
	REQUIRE(d->in_begin() != d->in_end());

	REQUIRE(a->out_begin() == a->out_end());
	REQUIRE(b->in_begin() == b->in_end());
}


TEST_CASE("Erase one of several edges") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	auto edge1 = graph.new_edge(a, b, 10);
	auto edge2 = graph.new_edge(a, c, 20);

	REQUIRE(graph.check());
	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 2);

	graph.erase_edge(edge1);

	REQUIRE(graph.check());

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 1);
	REQUIRE(b->in_begin() == b->in_end());
	REQUIRE(c->in_begin() != c->in_end());

	REQUIRE((*a->out_begin())->data() == 20);
	REQUIRE(edge2->data() == 20);
}


TEST_CASE("Erase leaf node") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);

	graph.new_edge(a, b, 10);
	graph.new_edge(a, c, 20);

	REQUIRE(graph.check());

	graph.erase_node(c);

	REQUIRE(graph.check());
	REQUIRE(std::distance(graph.begin(), graph.end()) == 2);

	REQUIRE(std::distance(a->out_begin(), a->out_end()) == 1);
	REQUIRE(std::distance(b->in_begin(), b->in_end()) == 1);
}


TEST_CASE("Erase node with incoming and outgoing edges") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);
	auto c = graph.new_node(3);
	auto d = graph.new_node(4);

	graph.new_edge(a, b, 10);
	graph.new_edge(b, c, 20);
	graph.new_edge(d, b, 30);

	REQUIRE(graph.check());

	graph.erase_node(b);

	REQUIRE(graph.check());

	REQUIRE(std::distance(graph.begin(), graph.end()) == 3);

	REQUIRE(a->out_begin() == a->out_end());
	REQUIRE(c->in_begin() == c->in_end());
	REQUIRE(d->out_begin() == d->out_end());
}


TEST_CASE("Copy assignment of graph") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(10);
	auto b = graph.new_node(20);

	graph.new_edge(a, b, 100);

	graph::orgraph_t<int, int> copy;

	copy = graph;

	REQUIRE(copy.check());
	REQUIRE(std::distance(copy.begin(), copy.end()) == 2);

	auto copy_a = copy.begin();
	auto copy_b = std::next(copy_a);

	REQUIRE(copy_a->data() == 10);
	REQUIRE(copy_b->data() == 20);

	REQUIRE(std::distance(
			copy_a->out_begin(),
			copy_a->out_end()
	) == 1);

	REQUIRE((*copy_a->out_begin())->data() == 100);
}


TEST_CASE("Move constructor of graph") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);

	graph.new_edge(a, b, 42);

	graph::orgraph_t<int, int> moved(std::move(graph));

	REQUIRE(moved.check());
	REQUIRE(std::distance(moved.begin(), moved.end()) == 2);

	auto root = moved.begin();

	REQUIRE(root->data() == 1);
	REQUIRE((*root->out_begin())->data() == 42);
}


TEST_CASE("Move assignment of graph") {
	graph::orgraph_t<int, int> graph;

	auto a = graph.new_node(1);
	auto b = graph.new_node(2);

	graph.new_edge(a, b, 42);

	graph::orgraph_t<int, int> moved;

	moved = std::move(graph);

	REQUIRE(moved.check());
	REQUIRE(std::distance(moved.begin(), moved.end()) == 2);

	auto root = moved.begin();

	REQUIRE(root->data() == 1);
	REQUIRE((*root->out_begin())->data() == 42);
}

TEST_CASE("Swap graphs") {
	graph::orgraph_t<int, int> first;
	graph::orgraph_t<int, int> second;

	auto a = first.new_node(1);
	auto b = first.new_node(2);

	first.new_edge(a, b, 100);

	second.new_node(10);

	REQUIRE(first.check());
	REQUIRE(second.check());

	first.swap(second);

	REQUIRE(first.check());
	REQUIRE(second.check());

	REQUIRE(std::distance(first.begin(), first.end()) == 1);
	REQUIRE(first.begin()->data() == 10);

	REQUIRE(std::distance(second.begin(), second.end()) == 2);
	REQUIRE(second.begin()->data() == 1);

	REQUIRE(std::distance(
			second.begin()->out_begin(),
			second.begin()->out_end()
	) == 1);

	REQUIRE(
			(*second.begin()->out_begin())->data() == 100
	);
}

TEST_CASE("Write graph to DOT file") {
	graph::orgraph_t<std::string, int> graph;

	auto a = graph.new_node("A");
	auto b = graph.new_node("B");
	auto c = graph.new_node("C");

	graph.new_edge(a, b, 10);
	graph.new_edge(b, c, 20);
	graph.new_edge(a, c, 30);

	std::ofstream output("graph.dot");
	REQUIRE(output.is_open());

	graph.write_dot(
			output,
			[](const std::string& data) {
				return data;
			},
			[](int data) {
				return std::to_string(data);
			}
	);

	REQUIRE(output.good());
}