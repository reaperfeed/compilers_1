"""Compile generated macro-only C with chibicc and execute the resulting ELF."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

FRONTEND, ROOT, CHIBICC, FIXTURES = [Path(x).resolve() for x in sys.argv[1:5]]
del sys.argv[1:]


class CodegenTests(unittest.TestCase):
    def command(self, args, cwd):
        result = subprocess.run([str(x) for x in args], cwd=cwd, text=True,
                                capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def build_run(self, cwd, generated, expected="", code=0, args=()):
        exe = cwd / "program"
        # Explicit intermediates also work in restricted environments without
        # a writable /tmp. Both C translation units are compiled by chibicc.
        objects = []
        for index, source in enumerate([generated, ROOT / "runtime/vm.c"]):
            assembly, obj = cwd / (str(index) + ".s"), cwd / (str(index) + ".o")
            self.command([CHIBICC, "-I" + str(ROOT / "runtime"), "-S", source, "-o", assembly], cwd)
            self.command([CHIBICC, "-c", assembly, "-o", obj], cwd)
            objects.append(obj)
        self.command([CHIBICC, *objects, "-o", exe], cwd)
        result = subprocess.run([str(exe), *args], cwd=cwd, text=True,
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        self.assertEqual(result.stdout, expected)
        return result

    def source(self, source, expected="", code=0, args=(), existing=False):
        with tempfile.TemporaryDirectory(prefix="c-codegen-") as directory:
            cwd = Path(directory)
            path = ROOT / source if existing else cwd / "input.lang"
            if not existing:
                path.write_text(source, encoding="utf-8")
            generated = cwd / "program.c"
            output = self.command([FRONTEND, "--emit-c", generated, path], cwd)
            self.assertIn("[C written to", output.stdout)
            self.assertNotIn("Program returned", output.stdout)
            text = generated.read_text()
            self.assertIn('extern', (ROOT / 'runtime/vm.h').read_text())
            self.assertIn("VM_MAIN(", text)
            # Generated bodies contain only macro calls and comments, no C locals
            # or direct expressions. Long macro calls may be on one source line.
            in_body = False
            for line in text.splitlines():
                if re.match(r"static void \w+\(void\) \{", line):
                    in_body = True
                    continue
                if in_body and line == "}":
                    in_body = False
                    continue
                if in_body and line.strip():
                    self.assertRegex(line.strip(), r"^(VM_[A-Z0-9_]+\(|/\*)")
            self.assertIsNone(re.search(r"\br\s*\[", text))
            return self.build_run(cwd, generated, expected, code, args)

    def test_constant_return(self):
        self.source("i32 main() { return 2 + 3; }", code=5)

    def test_invalid_main_signature(self):
        for text in ["f32 main() { return 1.0; }", "i32 main(bool argc) { return 0; }"]:
            with self.subTest(source=text), tempfile.TemporaryDirectory(prefix="c-main-") as directory:
                cwd = Path(directory)
                source = cwd / "input.lang"
                source.write_text(text)
                result = subprocess.run([str(FRONTEND), "--emit-c", "out.c", str(source)],
                                        cwd=cwd, text=True, capture_output=True, timeout=30)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("C codegen: main", result.stderr)

    def test_recursive_frames(self):
        self.source("""
            i32 fact(i32 n) { if (n <= 1) { return 1; } return n * fact(n - 1); }
            i32 main() { return fact(5); }
        """, code=120)

    def test_values_survive_calls(self):
        self.source("""
            i32 f(i32 x) { return x * 3; }
            i32 main() { i32 x = 7; return x + f(10) + f(1); }
        """, code=40)

    def test_more_than_six_arguments(self):
        self.source("""
            i32 sum(i32 a,i32 b,i32 c,i32 d,i32 e,i32 f,i32 g,i32 h) {
                return a+b+c+d+e+f+g+h;
            }
            i32 main() { return sum(1,2,3,4,5,6,7,8); }
        """, code=36)

    def test_short_circuit(self):
        self.source("""
            bool bad() { printi(99); return true; }
            i32 main() { bool a = false && bad(); bool b = true || bad();
                if (!a && b) { return 42; } return 0; }
        """, code=42)

    def test_float_arithmetic_and_comparisons(self):
        self.source("""
            f32 f(f32 x) { return -x / 2; }
            i32 main() { f32 x = f(7); printf("%g %f %d", x, x + 4, x);
                if (x < 0.25 && x != 0.0 && x <= -3.5 && x >= -3.5) { return 42; }
                return 0; }
        """, "-3.5 0.500000 -3", 42)

    def test_operators(self):
        self.source("""
            i32 main() { i32 x=5; x+=3; x-=1; x*=2; x/=2; x%=5;
                x|=8; x&=15; x^=1; x<<=1; x>>=1;
                printi(x); printi(~(-x)); printi(!(x==10)); return 0; }
        """, "1110true")

    def test_alias_and_expression_snapshot(self):
        self.source("""
            i32 change(ptr i32 p) { *p=10; return 1; }
            i32 main() { i32 x=2; printi(x + change(&x)); return x; }
        """, "3", 10)

    def test_nested_arrays(self):
        self.source("""
            i32 main() { i32[3][2] a; a[1][2]=7; ptr i32 p=&a[1][2];
                *p+=2; printi(a[1][2]+a[0][0]); printi("abc"[1]); return 0; }
        """, "9b")

    def test_void_and_mutual_recursion(self):
        self.source("""
            void show() { prints("V"); return; }
            bool even(i32 n) { if(n==0){return true;} return odd(n-1); }
            bool odd(i32 n) { if(n==0){return false;} return even(n-1); }
            i32 main() { show(); printi(even(8)); return 0; }
        """, "Vtrue")

    def test_runtime_file_and_heap(self):
        self.source("""
            i32 main() {
                i32 fd=open("roundtrip.txt",577,384);
                if(fd<0){return 1;}
                write(fd,"hello",5); close(fd);
                ptr char buffer=malloc(8);
                fd=open("roundtrip.txt",0,0);
                i32 count=read(fd,buffer,5); close(fd);
                buffer[5]='!'; write(1,buffer,6); free(buffer);
                if(count==5){return 0;} return 2;
            }
        """, "hello!")

    def test_argv(self):
        self.source("""
            i32 main(i32 argc, ptr ptr char argv) {
                printf("%d %s",argc,argv[1]); return 0;
            }
        """, "2 test-arg", args=("test-arg",))

    def test_utf8_and_escaped_strings(self):
        self.source('i32 main() { prints("Привет\\n\\\"\\\\"); return 0; }', 'Привет\n"\\')

    def test_division_by_zero_traps(self):
        result = self.source("i32 main() { return 1 / 0; }", code=2)
        self.assertIn("VM trap", result.stderr)

    def test_float_division_by_zero_traps(self):
        result = self.source("i32 main() { f32 value=1.0 / 0.0; printi(value); return 0; }", code=2)
        self.assertIn("VM trap", result.stderr)

    def test_print_and_write_order(self):
        self.source('i32 main() { prints("first"); write(1,"second",6); prints("third"); return 0; }',
                    "firstsecondthird")

    def test_recursive_depth_traps(self):
        result = self.source("i32 f(i32 n) { return f(n+1); } i32 main() { return f(0); }", code=2)
        self.assertIn("VM trap", result.stderr)

    def test_existing_arrays(self):
        self.source("tests/valid/arrays.lang", "array content: 0 1 4 9 16 25 36 49 64 81 \nsum of squares 0..9 = 285\n", existing=True)

    def test_existing_float(self):
        # Compare complete program output to the existing AST interpreter.
        self.compare_existing("float")

    def test_existing_fibonacci(self): self.compare_existing("fibonacci")
    def test_existing_pointers(self): self.compare_existing("pointers")
    def test_existing_loops(self): self.compare_existing("loops")
    def test_existing_minimal_number(self): self.compare_existing("minimal_number")

    def compare_existing(self, name):
        with tempfile.TemporaryDirectory(prefix="c-reference-") as directory:
            result = self.command([FRONTEND, ROOT / "tests/valid" / (name + ".lang")], Path(directory))
            stdout = result.stdout.split("[LIR written to lir.txt]\n", 1)[1]
            stdout = stdout.rsplit("\nProgram returned ", 1)[0]
            self.source("tests/valid/" + name + ".lang", stdout, existing=True)

    def test_lir_fixtures(self):
        for name, expected in [("aggregate", ""), ("global", "global!"),
                               ("indirect", ""), ("conversion", "")]:
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="c-fixture-") as directory:
                cwd = Path(directory)
                output = self.command([FIXTURES, name], cwd)
                path = cwd / "fixture.c"
                path.write_text(output.stdout)
                self.build_run(cwd, path, expected, 42)


if __name__ == "__main__":
    unittest.main(verbosity=2)
