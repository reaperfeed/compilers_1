#ifndef COMPILERS_VM_H
#define COMPILERS_VM_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* The VM is single-threaded. Addresses and words are 64 bits (Linux x86-64). */
#define VM_SP 14
#define VM_BP 15
#define VM_STACK_BYTES (16 * 1024 * 1024)
#define VM_MAX_ARGS 256
#define VM_MAX_DEPTH 256

/* A negative array bound makes an unsupported target fail during C parsing.
 * This form is also accepted by chibicc versions without _Static_assert. */
typedef char vm_supported_target[
    sizeof(void *) == 8 && sizeof(double) == 8 && CHAR_BIT == 8 ? 1 : -1];

enum {
    VM_T_VOID, VM_T_INT, VM_T_FLOAT, VM_T_BOOL, VM_T_CHAR,
    VM_T_STRING, VM_T_POINTER, VM_T_AGGREGATE
};

extern uint64_t r[16];
extern unsigned char vm_stack[VM_STACK_BYTES];
extern unsigned char vm_return_area[VM_STACK_BYTES];
extern uint64_t vm_args[VM_MAX_ARGS];
extern unsigned char vm_arg_types[VM_MAX_ARGS];
extern size_t vm_argc;
extern size_t vm_return_size;
extern size_t vm_depth;

void vm_init(void);
void vm_trap(void);
void vm_rt_print(void);
void vm_rt_printf(void);
void vm_rt_open(void);
void vm_rt_read(void);
void vm_rt_write(void);
void vm_rt_close(void);
void vm_rt_malloc(void);
void vm_rt_free(void);

#define VM_TRAP() do { vm_trap(); } while (0)
#define VM_MOV(D, S) do { r[(D)] = r[(S)]; } while (0)
#define VM_ZERO(D) do { r[(D)] = 0; } while (0)
#define VM_ADD(D, A, B) do { r[(D)] = r[(A)] + r[(B)]; } while (0)
#define VM_SUB(D, A, B) do { r[(D)] = r[(A)] - r[(B)]; } while (0)
#define VM_MUL(D, A, B) do { r[(D)] = r[(A)] * r[(B)]; } while (0)
#define VM_AND(D, A, B) do { r[(D)] = r[(A)] & r[(B)]; } while (0)
#define VM_OR(D, A, B) do { r[(D)] = r[(A)] | r[(B)]; } while (0)
#define VM_XOR(D, A, B) do { r[(D)] = r[(A)] ^ r[(B)]; } while (0)
#define VM_SHL(D, A, B) do { r[(D)] = r[(A)] << (r[(B)] & 63); } while (0)
#define VM_SHR(D, A, B) do { r[(D)] = (uint64_t)((int64_t)r[(A)] >> (r[(B)] & 63)); } while (0)
#define VM_NEG(D, S) do { r[(D)] = UINT64_C(0) - r[(S)]; } while (0)
#define VM_NOT(D, S) do { r[(D)] = !r[(S)]; } while (0)
#define VM_BITNOT(D, S) do { r[(D)] = ~r[(S)]; } while (0)
#define VM_EQ(D, A, B) do { r[(D)] = (r[(A)] == r[(B)]); } while (0)
#define VM_NE(D, A, B) do { r[(D)] = (r[(A)] != r[(B)]); } while (0)
#define VM_SLT(D, A, B) do { r[(D)] = ((int64_t)r[(A)] < (int64_t)r[(B)]); } while (0)
#define VM_SLE(D, A, B) do { r[(D)] = ((int64_t)r[(A)] <= (int64_t)r[(B)]); } while (0)
#define VM_SGT(D, A, B) do { r[(D)] = ((int64_t)r[(A)] > (int64_t)r[(B)]); } while (0)
#define VM_SGE(D, A, B) do { r[(D)] = ((int64_t)r[(A)] >= (int64_t)r[(B)]); } while (0)

/* Integer add/sub/mul wrap modulo 2^64. Division by zero traps. */
#define VM_SDIV(D, A, B) do { \
    int64_t vm_a_ = (int64_t)r[(A)], vm_b_ = (int64_t)r[(B)]; \
    if (!vm_b_) VM_TRAP(); \
    r[(D)] = (vm_a_ == INT64_MIN && vm_b_ == -1) \
        ? (uint64_t)INT64_MIN : (uint64_t)(vm_a_ / vm_b_); \
} while (0)
#define VM_SMOD(D, A, B) do { \
    int64_t vm_a_ = (int64_t)r[(A)], vm_b_ = (int64_t)r[(B)]; \
    if (!vm_b_) VM_TRAP(); \
    r[(D)] = (vm_a_ == INT64_MIN && vm_b_ == -1) \
        ? UINT64_C(0) : (uint64_t)(vm_a_ % vm_b_); \
} while (0)

/* Floating registers contain double bits, not a numeric cast to uint64_t. */
#define VM_FLOAT_BINARY(D, A, B, OP) do { \
    double vm_a_, vm_b_, vm_d_; \
    memcpy(&vm_a_, &r[(A)], 8); memcpy(&vm_b_, &r[(B)], 8); \
    vm_d_ = vm_a_ OP vm_b_; memcpy(&r[(D)], &vm_d_, 8); \
} while (0)
#define VM_FLOAT_COMPARE(D, A, B, OP) do { \
    double vm_a_, vm_b_; \
    memcpy(&vm_a_, &r[(A)], 8); memcpy(&vm_b_, &r[(B)], 8); \
    r[(D)] = (vm_a_ OP vm_b_); \
} while (0)
#define VM_FADD(D,A,B) VM_FLOAT_BINARY(D,A,B,+)
#define VM_FSUB(D,A,B) VM_FLOAT_BINARY(D,A,B,-)
#define VM_FMUL(D,A,B) VM_FLOAT_BINARY(D,A,B,*)
#define VM_FDIV(D,A,B) do { \
    double vm_divisor_; memcpy(&vm_divisor_, &r[(B)], 8); \
    if (vm_divisor_ == 0.0) VM_TRAP(); \
    VM_FLOAT_BINARY(D,A,B,/); \
} while (0)
#define VM_FEQ(D,A,B) VM_FLOAT_COMPARE(D,A,B,==)
#define VM_FNE(D,A,B) VM_FLOAT_COMPARE(D,A,B,!=)
#define VM_FLT(D,A,B) VM_FLOAT_COMPARE(D,A,B,<)
#define VM_FLE(D,A,B) VM_FLOAT_COMPARE(D,A,B,<=)
#define VM_FGT(D,A,B) VM_FLOAT_COMPARE(D,A,B,>)
#define VM_FGE(D,A,B) VM_FLOAT_COMPARE(D,A,B,>=)
#define VM_FNEG(D,S) do { \
    double vm_d_; memcpy(&vm_d_, &r[(S)], 8); \
    vm_d_ = -vm_d_; memcpy(&r[(D)], &vm_d_, 8); \
} while (0)
#define VM_I2F(D,S) do { \
    double vm_d_ = (double)(int64_t)r[(S)]; \
    memcpy(&r[(D)], &vm_d_, 8); \
} while (0)
#define VM_F2I(D,S) do { \
    double vm_d_; memcpy(&vm_d_, &r[(S)], 8); \
    if (!(vm_d_ >= -0x1p63 && vm_d_ < 0x1p63)) VM_TRAP(); \
    r[(D)] = (uint64_t)(int64_t)vm_d_; \
} while (0)
#define VM_BOOL(D,S) do { r[(D)] = !!r[(S)]; } while (0)
#define VM_CHAR(D,S) do { r[(D)] = (unsigned char)r[(S)]; } while (0)

/* memcpy supports byte-addressed storage without type-punning/alignment UB. */
#define VM_LOAD(D,A,T) do { \
    T vm_value_; memcpy(&vm_value_, (const void *)(uintptr_t)r[(A)], sizeof(T)); \
    r[(D)] = vm_value_; \
} while (0)
#define VM_STORE(A,S,T) do { \
    T vm_value_ = (T)r[(S)]; \
    memcpy((void *)(uintptr_t)r[(A)], &vm_value_, sizeof(T)); \
} while (0)
#define VM_LD8(D,A) VM_LOAD(D,A,uint8_t)
#define VM_LD16(D,A) VM_LOAD(D,A,uint16_t)
#define VM_LD32(D,A) VM_LOAD(D,A,uint32_t)
#define VM_LD64(D,A) VM_LOAD(D,A,uint64_t)
#define VM_ST8(A,S) VM_STORE(A,S,uint8_t)
#define VM_ST16(A,S) VM_STORE(A,S,uint16_t)
#define VM_ST32(A,S) VM_STORE(A,S,uint32_t)
#define VM_ST64(A,S) VM_STORE(A,S,uint64_t)
#define VM_COPY(D,S,N) do { \
    memmove((void *)(uintptr_t)r[(D)], (const void *)(uintptr_t)r[(S)], (size_t)r[(N)]); \
} while (0)
#define VM_ADDR(D,OBJECT) do { r[(D)] = (uint64_t)(uintptr_t)&(OBJECT); } while (0)
#define VM_FADDR(D,FUNCTION) do { r[(D)] = (uint64_t)(uintptr_t)&(FUNCTION); } while (0)
#define VM_LABEL(L) L: ;
#define VM_JUMP(L) do { goto L; } while (0)
#define VM_BRANCH(C,T,F) do { if (r[(C)]) goto T; else goto F; } while (0)
#define VM_CALL(FUNCTION) do { (FUNCTION)(); } while (0)
#define VM_ICALL(F) do { ((void (*)(void))(uintptr_t)r[(F)])(); } while (0)
#define VM_RET() do { return; } while (0)
#define VM_OPEN() VM_CALL(vm_rt_open)
#define VM_READ() VM_CALL(vm_rt_read)
#define VM_WRITE() VM_CALL(vm_rt_write)
#define VM_CLOSE() VM_CALL(vm_rt_close)
#define VM_MALLOC() VM_CALL(vm_rt_malloc)
#define VM_FREE() VM_CALL(vm_rt_free)

#define VM_STACK_ALLOC(N) do { \
    if (r[VM_SP] < (uint64_t)(uintptr_t)vm_stack || \
        r[VM_SP] > (uint64_t)(uintptr_t)(vm_stack + VM_STACK_BYTES) || \
        r[(N)] > r[VM_SP] - (uint64_t)(uintptr_t)vm_stack) VM_TRAP(); \
    r[VM_SP] -= r[(N)]; \
} while (0)
#define VM_PUSH(S) do { \
    if (r[VM_SP] < (uint64_t)(uintptr_t)(vm_stack + 8) || \
        r[VM_SP] > (uint64_t)(uintptr_t)(vm_stack + VM_STACK_BYTES)) VM_TRAP(); \
    r[VM_SP] -= 8; \
    memcpy((void *)(uintptr_t)r[VM_SP], &r[(S)], 8); \
} while (0)
#define VM_POP(D) do { \
    if (r[VM_SP] < (uint64_t)(uintptr_t)vm_stack || \
        r[VM_SP] > (uint64_t)(uintptr_t)(vm_stack + VM_STACK_BYTES - 8)) VM_TRAP(); \
    memcpy(&r[(D)], (const void *)(uintptr_t)r[VM_SP], 8); \
    r[VM_SP] += 8; \
} while (0)
#define VM_ENTER(SIZE_REG) do { \
    if (++vm_depth > VM_MAX_DEPTH) VM_TRAP(); \
    VM_PUSH(VM_BP); VM_MOV(VM_BP,VM_SP); VM_STACK_ALLOC(SIZE_REG); \
} while (0)
#define VM_LEAVE() do { \
    if (!vm_depth) VM_TRAP(); \
    VM_MOV(VM_SP,VM_BP); VM_POP(VM_BP); --vm_depth; \
} while (0)

/* First six scalar arguments are in r[0..5]; extras use the argument bank.
 * All arguments are mirrored in the bank for variadic runtime functions.
 * Every generated callee copies incoming arguments to its frame before calls. */
#define VM_ARGS_BEGIN() do { vm_argc = 0; } while (0)
#define VM_SETARG(I,S,T) do { \
    if ((size_t)(I) >= VM_MAX_ARGS) VM_TRAP(); \
    vm_args[(I)] = r[(S)]; vm_arg_types[(I)] = (T); \
    if ((I) < 6) r[(I)] = r[(S)]; \
    if (vm_argc <= (size_t)(I)) vm_argc = (size_t)(I) + 1; \
} while (0)
#define VM_GETARG(D,I) do { \
    if ((size_t)(I) >= vm_argc) VM_TRAP(); \
    r[(D)] = ((I) < 6) ? r[(I)] : vm_args[(I)]; \
} while (0)
#define VM_GETARG_BLOCK(D,I,N) do { \
    if ((size_t)(I) >= vm_argc) VM_TRAP(); \
    memmove((void *)(uintptr_t)r[(D)], (const void *)(uintptr_t)vm_args[(I)], (size_t)r[(N)]); \
} while (0)
#define VM_SETRET(S) VM_MOV(0,S)
#define VM_GETRET(D) VM_MOV(D,0)
/* Copy aggregate returns before releasing the callee frame. */
#define VM_SETRET_BLOCK(A,N) do { \
    if (r[(N)] > VM_STACK_BYTES) VM_TRAP(); \
    vm_return_size = (size_t)r[(N)]; \
    memmove(vm_return_area, (const void *)(uintptr_t)r[(A)], vm_return_size); \
} while (0)
#define VM_GETRET_BLOCK(A,N) do { \
    if (r[(N)] != vm_return_size) VM_TRAP(); \
    memmove((void *)(uintptr_t)r[(A)], vm_return_area, vm_return_size); \
} while (0)

/* The host entry is the only generated C function with native arguments. */
#define VM_MAIN(ENTRY) \
    int main(int argc, char **argv) { \
        vm_init(); \
        r[0] = (uint64_t)argc; r[1] = (uint64_t)(uintptr_t)argv; \
        vm_args[0] = r[0]; vm_args[1] = r[1]; \
        vm_arg_types[0] = VM_T_INT; vm_arg_types[1] = VM_T_POINTER; \
        vm_argc = 2; (ENTRY)(); return (int)r[0]; \
    }

#endif
