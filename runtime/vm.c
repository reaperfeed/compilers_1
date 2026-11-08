#include "vm.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

uint64_t r[16];
_Alignas(16) unsigned char vm_stack[VM_STACK_BYTES];
_Alignas(16) unsigned char vm_return_area[VM_STACK_BYTES];
uint64_t vm_args[VM_MAX_ARGS];
unsigned char vm_arg_types[VM_MAX_ARGS];
size_t vm_argc;
size_t vm_return_size;
size_t vm_depth;

void vm_trap(void) {
    fputs("VM trap\n", stderr);
    exit(2);
}

void vm_init(void) {
    memset(r, 0, sizeof(r));
    vm_argc = vm_return_size = vm_depth = 0;
    r[VM_SP] = (uint64_t)(uintptr_t)(vm_stack + sizeof(vm_stack));
    r[VM_BP] = r[VM_SP];
}

/* prints and printi accept the same scalar kinds as the source builtins. */
void vm_rt_print(void) {
    double value;
    if (vm_argc != 1) vm_trap();
    switch (vm_arg_types[0]) {
    case VM_T_INT: printf("%lld", (long long)(int64_t)vm_args[0]); break;
    case VM_T_FLOAT:
        memcpy(&value, &vm_args[0], 8); printf("%g", value); break;
    case VM_T_BOOL: fputs(vm_args[0] ? "true" : "false", stdout); break;
    case VM_T_CHAR: putchar((unsigned char)vm_args[0]); break;
    case VM_T_STRING:
        if (!vm_args[0]) vm_trap();
        fputs((const char *)(uintptr_t)vm_args[0], stdout); break;
    default: vm_trap();
    }
    r[0] = 0;
}

/* Match the language's supported format subset, without assuming the host
 * variadic ABI can interpret uint64_t register bits as double arguments. */
void vm_rt_printf(void) {
    const char *format;
    size_t arg = 1;
    double real;
    if (!vm_argc || !vm_args[0]) vm_trap();
    format = (const char *)(uintptr_t)vm_args[0];
    while (*format) {
        char spec = *format++;
        if (spec != '%' || !*format) { putchar(spec); continue; }
        spec = *format++;
        if (spec == '%') { putchar('%'); continue; }
        if (arg >= vm_argc) vm_trap();
        if (vm_arg_types[arg] == VM_T_FLOAT)
            memcpy(&real, &vm_args[arg], 8);
        else
            real = (double)(int64_t)vm_args[arg];
        switch (spec) {
        case 'd':
            if (vm_arg_types[arg] == VM_T_FLOAT) {
                if (!(real >= -0x1p63 && real < 0x1p63)) vm_trap();
                printf("%lld", (long long)(int64_t)real);
            } else printf("%lld", (long long)(int64_t)vm_args[arg]);
            break;
        case 'f': printf("%f", real); break;
        case 'g': printf("%g", real); break;
        case 'c': putchar((unsigned char)vm_args[arg]); break;
        case 's':
            if (vm_arg_types[arg] != VM_T_STRING || !vm_args[arg]) vm_trap();
            fputs((const char *)(uintptr_t)vm_args[arg], stdout); break;
        default: vm_trap();
        }
        ++arg;
    }
    r[0] = 0;
}

/* These no-argument wrappers use the same r[0..] calling convention. */
void vm_rt_open(void) {
    r[0] = (uint64_t)(int64_t)open((const char *)(uintptr_t)r[0],
                                  (int)r[1], (mode_t)r[2]);
}
void vm_rt_read(void) {
    r[0] = (uint64_t)(int64_t)read((int)r[0], (void *)(uintptr_t)r[1], (size_t)r[2]);
}
void vm_rt_write(void) {
    /* Keep print builtins ordered with direct writes to the same host stream. */
    fflush(stdout);
    fflush(stderr);
    r[0] = (uint64_t)(int64_t)write((int)r[0], (const void *)(uintptr_t)r[1], (size_t)r[2]);
}
void vm_rt_close(void) {
    if ((int)r[0] == 1) fflush(stdout);
    if ((int)r[0] == 2) fflush(stderr);
    r[0] = (uint64_t)(int64_t)close((int)r[0]);
}
void vm_rt_malloc(void) { r[0] = (uint64_t)(uintptr_t)malloc((size_t)r[0]); }
void vm_rt_free(void) { free((void *)(uintptr_t)r[0]); r[0] = 0; }
