#include "ctffi.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG(...) do { fprintf(stderr, "[ctffi-phase2] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: " __VA_ARGS__); return 1; } while (0)

static int prepare(ffi_cif *cif, unsigned n, ffi_type *ret,
                   ffi_type **args, const char *name) {
    ffi_status s = ffi_prep_cif(cif, FFI_DEFAULT_ABI, n, ret, args);
    LOG("ffi_prep_cif(%s): status=%d", name, s);
    return s == FFI_OK ? 0 : -1;
}

static int same_type(const ffi_type *a, const ffi_type *b) {
    if (!a || !b)
        return a == b;
    return a->type == b->type && a->size == b->size && a->alignment == b->alignment;
}

static int build_auto(const char *path, const char *name, ctf_ffi_context_t *ctx,
                      ffi_cif *cif, ffi_type **ret, ffi_type ***args, size_t *n) {
    if (ctf_ffi_init(ctx, path) != 0)
        return -1;
    if (build_cif_from_ctf(ctx, name, cif, ret, args, n) != 0) {
        ctf_ffi_cleanup(ctx);
        return -1;
    }
    return 0;
}

static void *symbol(void *handle, const char *name) {
    void *p = dlsym(handle, name);
    if (!p)
        LOG("dlsym(%s): %s", name, dlerror());
    return p;
}

static ffi_type *int_type(void) {
    switch (sizeof(int)) {
    case 1: return &ffi_type_sint8;
    case 2: return &ffi_type_sint16;
    case 4: return &ffi_type_sint32;
    case 8: return &ffi_type_sint64;
    default: return NULL;
    }
}

static ffi_type *ulong_type(void) {
    switch (sizeof(unsigned long)) {
    case 1: return &ffi_type_uint8;
    case 2: return &ffi_type_uint16;
    case 4: return &ffi_type_uint32;
    case 8: return &ffi_type_uint64;
    default: return NULL;
    }
}

static int compare_simple(const char *name, const ffi_cif *automatic,
                          ffi_type *manual_ret, unsigned n, ffi_type **manual_args) {
    ffi_cif manual;
    if (prepare(&manual, n, manual_ret, manual_args, name) != 0)
        return -1;
    if (automatic->nargs != manual.nargs || !same_type(automatic->rtype, manual.rtype))
        return -1;
    for (unsigned i = 0; i < n; ++i)
        if (!same_type(automatic->arg_types[i], manual.arg_types[i]))
            return -1;
    return 0;
}

static int test_integer_encodings(const char *path) {
    LOG("=== TEST 1: integer encodings and enum ===");
    ffi_type *it = int_type();
    ffi_type *ult = ulong_type();
    if (!it || !ult)
        FAIL("unsupported native integer size in test harness");

    const char *names[] = { "accept_char", "accept_uchar", "accept_short",
                            "accept_ulong", "accept_bool", "enum_value" };
    ffi_type *manual_args[] = { &ffi_type_schar, &ffi_type_uchar, &ffi_type_sint16,
                                ult, &ffi_type_uint8, it };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        ctf_ffi_context_t ctx;
        ffi_cif cif;
        ffi_type *ret = NULL, **args = NULL;
        size_t n = 0;
        if (build_auto(path, names[i], &ctx, &cif, &ret, &args, &n) != 0)
            FAIL("automatic CIF failed for %s", names[i]);
        ffi_type *ma[] = { manual_args[i] };
        if (n != 1 || compare_simple(names[i], &cif, manual_args[i], 1, ma) != 0) {
            free(args);
            ctf_ffi_cleanup(&ctx);
            FAIL("CIF mismatch for %s", names[i]);
        }
        free(args);
        ctf_ffi_cleanup(&ctx);
    }

    void *h = dlopen(path, RTLD_NOW);
    if (!h)
        FAIL("dlopen failed: %s", dlerror());

    char c = 'A'; unsigned char uc = 250; short s = -1234;
    unsigned long ul = 0x12345678UL; _Bool b = 1;
    int e = 7;
    int result = 0;
    void *v[] = { &c };
    ffi_cif cif;
    ctf_ffi_context_t ctx;
    ffi_type *ret = NULL, **args = NULL;
    size_t n = 0;

    if (build_auto(path, "accept_char", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_char automatic CIF failed");
    ffi_call(&cif, FFI_FN(symbol(h, "accept_char")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx);
    if (result != 'A') FAIL("accept_char result mismatch: %d", result);

    if (build_auto(path, "accept_uchar", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_uchar automatic CIF failed");
    v[0] = &uc; result = 0;
    ffi_call(&cif, FFI_FN(symbol(h, "accept_uchar")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx);
    if (result != 250) FAIL("accept_uchar result mismatch: %d", result);

    if (build_auto(path, "accept_short", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_short automatic CIF failed");
    v[0] = &s; result = 0;
    ffi_call(&cif, FFI_FN(symbol(h, "accept_short")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx);
    if (result != -1234) FAIL("accept_short result mismatch: %d", result);

    if (build_auto(path, "accept_ulong", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_ulong automatic CIF failed");
    unsigned long result_ul = 0;
    v[0] = &ul;
    ffi_call(&cif, FFI_FN(symbol(h, "accept_ulong")), &result_ul, v);
    free(args); ctf_ffi_cleanup(&ctx);
    if (result_ul != ul) FAIL("accept_ulong result mismatch");

    if (build_auto(path, "accept_bool", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_bool automatic CIF failed");
    v[0] = &b; result = 0;
    ffi_call(&cif, FFI_FN(symbol(h, "accept_bool")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx);
    if (result != 1) FAIL("accept_bool result mismatch: %d", result);

    if (build_auto(path, "enum_value", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("enum_value automatic CIF failed");
    v[0] = &e; result = 0;
    ffi_call(&cif, FFI_FN(symbol(h, "enum_value")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx);
    dlclose(h);
    if (result != 7) FAIL("enum_value result mismatch: %d", result);
    return 0;
}

static int test_typedef_and_qualifiers(const char *path) {
    LOG("=== TEST 2: typedef and qualified types ===");
    const char *names[] = { "accept_typedef", "accept_const_scalar" };
    ffi_type *it = int_type();
    if (!it)
        FAIL("unsupported native int size in test harness");

    for (size_t i = 0; i < 2; ++i) {
        ctf_ffi_context_t ctx;
        ffi_cif cif;
        ffi_type *ret = NULL, **args = NULL;
        size_t n = 0;
        if (build_auto(path, names[i], &ctx, &cif, &ret, &args, &n) != 0)
            FAIL("automatic CIF failed for %s", names[i]);
        ffi_type *ma[] = { it };
        if (compare_simple(names[i], &cif, it, 1, ma) != 0) {
            free(args); ctf_ffi_cleanup(&ctx);
            FAIL("CIF mismatch for %s", names[i]);
        }
        free(args); ctf_ffi_cleanup(&ctx);
    }

    void *h = dlopen(path, RTLD_NOW);
    if (!h) FAIL("dlopen failed: %s", dlerror());
    int value = 42, result = 0;
    void *v[] = { &value };
    ctf_ffi_context_t ctx;
    ffi_cif cif;
    ffi_type *ret = NULL, **args = NULL;
    size_t n = 0;
    if (build_auto(path, "accept_const_scalar", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("accept_const_scalar automatic CIF failed");
    ffi_call(&cif, FFI_FN(symbol(h, "accept_const_scalar")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx); dlclose(h);
    if (result != 42) FAIL("accept_const_scalar result mismatch: %d", result);
    return 0;
}

static int test_function_pointer(const char *path) {
    LOG("=== TEST 3: function type / function pointer ===");
    ffi_type *it = int_type();
    if (!it)
        FAIL("unsupported native int size in test harness");
    ctf_ffi_context_t ctx;
    ffi_cif cif;
    ffi_type *ret = NULL, **args = NULL;
    size_t n = 0;
    if (build_auto(path, "apply_operation", &ctx, &cif, &ret, &args, &n) != 0)
        FAIL("apply_operation automatic CIF failed");
    ffi_type *ma[] = { it, it, &ffi_type_pointer };
    if (compare_simple("apply_operation", &cif, it, 3, ma) != 0) {
        free(args); ctf_ffi_cleanup(&ctx);
        FAIL("apply_operation CIF mismatch");
    }

    void *h = dlopen(path, RTLD_NOW);
    if (!h) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    int a = 7, b = 5, result = 0;
    int (*operation)(int, int) = NULL;
    *(void **)(&operation) = symbol(h, "add_numbers");
    void *v[] = { &a, &b, &operation };
    ffi_call(&cif, FFI_FN(symbol(h, "apply_operation")), &result, v);
    free(args); ctf_ffi_cleanup(&ctx); dlclose(h);
    if (result != 12) FAIL("apply_operation result mismatch: %d", result);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]);
        return 2;
    }
    if (test_integer_encodings(argv[1]) ||
        test_typedef_and_qualifiers(argv[1]) ||
        test_function_pointer(argv[1])) {
        LOG("RESULT: FAILED");
        return 1;
    }
    LOG("RESULT: ALL PHASE 2 TESTS PASSED");
    return 0;
}
