"""End-to-end checks: parser -> semantic analysis -> HIR -> verifier -> CFG -> AST runtime."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = Path(sys.argv[1]).resolve()
TESTS = Path(sys.argv[2]).resolve()
del sys.argv[1:]


class FrontendTests(unittest.TestCase):
    def invoke(self, source, *, existing=False, check_only=False, args=()):
        with tempfile.TemporaryDirectory(prefix="frontend-", dir=Path.cwd()) as temp:
            cwd = Path(temp)
            if existing:
                path = TESTS / source
            else:
                path = cwd / "input.lang"
                path.write_text(source, encoding="utf-8")
            command = [str(EXECUTABLE)]
            if check_only:
                command.append("--check-only")
            command += [str(path), *args]
            result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=30)
            outputs = {name: (cwd / name).read_text() for name in ("hir.txt", "cfg.dot") if (cwd / name).exists()}
            return result, outputs

    def valid(self, source, expected=None, *, existing=False, args=()):
        result, outputs = self.invoke(source, existing=existing, args=args)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("function", outputs["hir.txt"])
        self.assertIn("digraph CFG", outputs["cfg.dot"])
        if expected:
            self.assertIn(expected, result.stdout)
        return result, outputs

    def test_original_valid_programs(self):
        expected = {
            "arrays": "sum of squares 0..9 = 285",
            "fibonacci": "fib(20) = 6765",
            "float": "mix  = 3.5",
            "hello": "Option: -test",
            "loops": "5! = 120",
            "minimal_number": "20\n20",
            "pointers": "x after *p = 99: 99",
        }
        for name, output in expected.items():
            with self.subTest(name=name):
                self.valid(f"valid/{name}.lang", output, existing=True, args=("-test",))

    def test_original_invalid_programs(self):
        for path in sorted((TESTS / "invalid").glob("*.lang")):
            with self.subTest(name=path.name):
                result, _ = self.invoke(f"invalid/{path.name}", existing=True)
                expected = 2 if path.stem in {"deep_recursion", "runtime_error"} else 1
                self.assertEqual(result.returncode, expected, result.stderr + result.stdout)

    def test_check_only_does_not_execute_program(self):
        result, outputs = self.invoke('i32 main() { return 1 / 0; }', check_only=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("div", outputs["hir.txt"])
        self.assertNotIn("Program returned", result.stdout)

    def test_forward_and_mutually_recursive_calls(self):
        self.valid('''
            i32 main() { printi(twice(6)); printi(even(8)); return 0; }
            i32 twice(i32 n) { return n * 2; }
            bool even(i32 n) { if (n == 0) { return true; } return odd(n - 1); }
            bool odd(i32 n) { if (n == 0) { return false; } return even(n - 1); }
        ''', "12true")

    def test_implicit_float_conversions_match_runtime(self):
        _, outputs = self.valid('''
            f32 half(f32 n) { return n / 2; }
            f32 count() { return 5; }
            i32 main() { f32 a = 5; a = 7;
                printf("%g %g %g", a / 2, half(5), count() / 2); return 0; }
        ''', "3.5 2.5 2.5")
        self.assertGreaterEqual(outputs["hir.txt"].count("convert"), 6)

    def test_short_circuit_skips_side_effects(self):
        result, outputs = self.valid('''
            bool effect() { prints("BAD"); return true; }
            i32 main() {
                bool a = false && effect(); bool b = true || effect();
                bool changed = false;
                bool c = a && (changed = true); bool d = b || (changed = true);
                if (changed) { return 1; } return 0;
            }
        ''', "Program returned 0")
        self.assertNotIn("BAD", result.stdout)
        self.assertIn("branch", outputs["hir.txt"])

    def test_short_circuit_evaluates_required_rhs(self):
        self.valid('''
            bool effect() { prints("RHS"); return true; }
            i32 main() { bool a = true && effect(); bool b = false || effect();
                if (a && b) { return 7; } return 0; }
        ''', "RHSRHS\nProgram returned 7")

    def test_left_to_right_values_are_snapshots(self):
        self.valid('i32 main() { i32 x = 1; printi(x + (x = 2)); return x; }', "3\nProgram returned 2")
        self.valid('''
            i32 change(ptr i32 p) { *p = 10; return 1; }
            i32 main() { i32 x = 2; printi(x + change(&x)); return x; }
        ''', "3\nProgram returned 10")

    def test_calls_at_end_of_branches_and_loop_bodies(self):
        self.valid('''
            i32 main() {
                if (true) { prints("A"); } else { prints("BAD"); }
                i32 i = 0; while (i < 2) { i += 1; prints("B"); }
                i32 j = 0; for (j = 0; j < 2; j += 1) { prints("C"); }
                return 0;
            }
        ''', "ABBCC\nProgram returned 0")

    def test_arrays_nested_indices_and_pointer_stores(self):
        self.valid('''
            i32 main() { i32[3][2] a; a[1][2] = 7;
                ptr i32 p = &a[1][2]; *p += 2;
                printi(a[1][2] + a[0][0]); printi("abc"[1]); return 0; }
        ''', "9b")

    def test_index_subexpressions_are_evaluated_once(self):
        self.valid('''
            i32 main() { i32[3] a; a[1] = 4; a[2] = 5;
                i32 i = 0; printi(a[i += 1]); return i; }
        ''', "4\nProgram returned 1")

    def test_pointer_returns_equality_and_scalar_index(self):
        self.valid('''
            ptr i32 identity(ptr i32 p) { return p; }
            i32 main() { i32 x = 3; ptr i32 p = identity(&x); p[0] += 2;
                if (p == &x) { prints("same"); } return x; }
        ''', "same\nProgram returned 5")

    def test_scope_shadowing_and_zero_initialization(self):
        self.valid('''
            i32 main() { i32 x = 7; { i32 x = x + 1; printi(x); }
                printi(x); i32 zero; printi(zero); return 0; }
        ''', "870")

    def test_void_functions_and_early_return(self):
        self.valid('''
            void empty() { }
            void show() { prints("V"); return; }
            i32 choose(bool b) { if (b) { return 4; } else { return 5; } }
            i32 main() { empty(); show(); return choose(false); }
        ''', "V\nProgram returned 5")

    def test_all_operators(self):
        self.valid('''
            i32 main() { i32 x = 5; x += 3; x -= 1; x *= 2; x /= 2;
                x %= 5; x |= 8; x &= 15; x ^= 1; x <<= 1; x >>= 1;
                i32 y = ~(-x); bool b = !(x == y) && (x != y);
                printi(x); printi(y); printi(b); return 0; }
        ''', "1110true")

    def test_strict_source_errors(self):
        cases = [
            'i32 main() { void x; return 0; }',
            'i32 main() { i32 x; bool b; ptr i32 p = &x; p = &b; return 0; }',
            'i32 main() { i32[2] a; return a[1.5]; }',
            'i32 main() { i32 f; f(); return 0; }',
            'i32 main() { ptr i32 p = &(1 + 2); return 0; }',
            'i32 main() { return *5; }',
            'i32 main() { i32 x; ptr i32 p = &x; *p = true; return 0; }',
            'i32 main() { return 5 % 2.5; }',
            'i32 main() { return later(true); } i32 later(i32 n) { return n; }',
            'void v() { } i32 main() { i32 x = v(); return 0; }',
            'i32 main() { ptr i32 p = &missing; return 0; }',
            'i32 f(i32 x, i32 x) { return x; }',
            'i32 main() { i32 x = x; return 0; }',
            'i32 main() { ptr i32 p; if (p) { return 1; } return 0; }',
        ]
        for source in cases:
            with self.subTest(source=source):
                result, _ = self.invoke(source, check_only=True)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("error:", result.stderr)

    def test_multiline_comment_locations(self):
        result, _ = self.invoke('i32 main() {\n/* first\n second */\n @\n}')
        self.assertEqual(result.returncode, 1)
        self.assertIn(":4:2: error: [lexical]", result.stderr)

    def test_explicit_output_paths(self):
        with tempfile.TemporaryDirectory(prefix="paths-", dir=Path.cwd()) as temp:
            cwd = Path(temp)
            source = cwd / "source.lang"
            source.write_text("i32 main() { return 0; }")
            result = subprocess.run([str(EXECUTABLE), "--check-only", "--hir", "out.hir", "--cfg", "out.dot",
                                     "--ast", "tree.dot", str(source)], cwd=cwd, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in ("out.hir", "out.dot", "tree.dot"):
                self.assertGreater((cwd / name).stat().st_size, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
