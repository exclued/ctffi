/* Test program to manually create CTF-like type info for libffi */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <ffi.h>

/* Manual type definitions that would normally come from CTF */
typedef struct {
    const char *name;
    ffi_type *type;
    size_t size;
} type_info_t;

/* Common type mappings */
static type_info_t common_types[] = {
    { "int", &ffi_type_sint32, sizeof(int32_t) },
    { "long", &ffi_type_sint64, sizeof(int64_t) },
    { "short", &ffi_type_sint16, sizeof(int16_t) },
    { "char", &ffi_type_sint8, sizeof(int8_t) },
    { "float", &ffi_type_float, sizeof(float) },
    { "double", &ffi_type_double, sizeof(double) },
    { "void*", &ffi_type_pointer, sizeof(void*) },
    { NULL, NULL, 0 }
};

/* Example: Manually construct FFI call for add_numbers(int, int) */
void test_add_numbers(void *handle) {
    void (*add_numbers)(int, int) = dlsym(handle, "add_numbers");
    if (!add_numbers) {
        fprintf(stderr, "Failed to find add_numbers: %s\n", dlerror());
        return;
    }
    
    /* Manual FFI setup (what CTF would automate) */
    ffi_cif cif;
    ffi_type *args[2];
    void *arg_values[2];
    int arg0 = 5, arg1 = 3;
    int result;
    
    args[0] = &ffi_type_sint32;
    args[1] = &ffi_type_sint32;
    
    arg_values[0] = &arg0;
    arg_values[1] = &arg1;
    
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, args) == FFI_OK) {
        ffi_call(&cif, FFI_FN(add_numbers), &result, arg_values);
        printf("add_numbers(5, 3) via FFI = %d\n", result);
    }
}

/* Example: Manually construct FFI call for compute(double, float, int) */
void test_compute(void *handle) {
    double (*compute)(double, float, int) = dlsym(handle, "compute");
    if (!compute) {
        fprintf(stderr, "Failed to find compute: %s\n", dlerror());
        return;
    }
    
    ffi_cif cif;
    ffi_type *args[3];
    void *arg_values[3];
    double arg0 = 2.5;
    float arg1 = 4.0f;
    int arg2 = 10;
    double result;
    
    args[0] = &ffi_type_double;
    args[1] = &ffi_type_float;
    args[2] = &ffi_type_sint32;
    
    arg_values[0] = &arg0;
    arg_values[1] = &arg1;
    arg_values[2] = &arg2;
    
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 3, &ffi_type_double, args) == FFI_OK) {
        ffi_call(&cif, FFI_FN(compute), &result, arg_values);
        printf("compute(2.5, 4.0f, 10) via FFI = %f\n", result);
    }
}

/* Example: Function returning pointer */
void test_get_message(void *handle) {
    char* (*get_message)(void) = dlsym(handle, "get_message");
    if (!get_message) {
        fprintf(stderr, "Failed to find get_message: %s\n", dlerror());
        return;
    }
    
    ffi_cif cif;
    char *result;
    
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_pointer, NULL) == FFI_OK) {
        ffi_call(&cif, FFI_FN(get_message), &result, NULL);
        printf("get_message() via FFI = %s\n", result);
    }
}

int main(int argc, char *argv[]) {
    const char *lib_path = (argc > 1) ? argv[1] : "./build/libtest_ctf.so";
    
    void *handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Failed to load library: %s\n", dlerror());
        return 1;
    }
    
    printf("=== Testing Manual FFI (simulating what CTF would provide) ===\n\n");
    
    test_add_numbers(handle);
    test_compute(handle);
    test_get_message(handle);
    
    printf("\nNote: In a full implementation, CTF metadata would automatically\n");
    printf("provide the type information used above, eliminating manual setup.\n");
    
    dlclose(handle);
    return 0;
}
