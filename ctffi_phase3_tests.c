#include "ctffi.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int values[3];
    char tag;
} IntArrayRecord;

typedef struct {
    unsigned short matrix[2][3];
    long total;
} MatrixRecord;

#define LOG(...) do { fprintf(stderr, "[ctffi-phase3] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: " __VA_ARGS__); return 1; } while (0)

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

static int prepare(ffi_cif *cif, unsigned n, ffi_type *ret,
                   ffi_type **args, const char *name) {
    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, n, ret, args);
    LOG("ffi_prep_cif(%s): status=%d", name, status);
    return status == FFI_OK ? 0 : -1;
}

static int same_type(const ffi_type *a, const ffi_type *b) {
    return a && b && a->type == b->type && a->size == b->size && a->alignment == b->alignment;
}

static int test_array_member(const char *path, void *handle) {
    LOG("=== TEST 1: fixed array inside aggregate ===");
    ctf_ffi_context_t ctx;
    ffi_cif cif;
    ffi_type *ret = NULL, **args = NULL;
    size_t nargs = 0;
    if (build_auto(path, "sum_array_record", &ctx, &cif, &ret, &args, &nargs) != 0)
        FAIL("automatic CIF failed for sum_array_record");

    ffi_type array = {0};
    ffi_type *array_elements[] = { &ffi_type_sint32, &ffi_type_sint32,
                                   &ffi_type_sint32, NULL };
    array.type = FFI_TYPE_STRUCT;
    array.elements = array_elements;
    if (prepare(&(ffi_cif){0}, 0, &array, NULL, "array") != 0)
        FAIL("manual array layout failed");

    ffi_type record = {0};
    ffi_type *record_elements[] = { &array, &ffi_type_schar, NULL };
    record.type = FFI_TYPE_STRUCT;
    record.elements = record_elements;
    if (prepare(&(ffi_cif){0}, 0, &record, NULL, "record") != 0)
        FAIL("manual record layout failed");

    ffi_type *manual_args[] = { &record };
    ffi_cif manual;
    if (prepare(&manual, 1, &ffi_type_sint32, manual_args, "sum_array_record") != 0)
        FAIL("manual CIF failed");
    if (nargs != 1 || !same_type(cif.rtype, manual.rtype) || !same_type(cif.arg_types[0], manual.arg_types[0]))
        FAIL("automatic and manual CIFs differ for sum_array_record");

    IntArrayRecord value = {{1, 2, 3}, 4};
    int result = 0;
    void *values[] = { &value };
    ffi_call(&cif, FFI_FN(dlsym(handle, "sum_array_record")), &result, values);
    if (result != 10)
        FAIL("sum_array_record result mismatch: %d", result);

    free(args);
    ctf_ffi_cleanup(&ctx);
    return 0;
}

static int test_array_return(const char *path, void *handle) {
    LOG("=== TEST 2: aggregate containing array returned by value ===");
    ctf_ffi_context_t ctx;
    ffi_cif cif;
    ffi_type *ret = NULL, **args = NULL;
    size_t nargs = 0;
    if (build_auto(path, "make_array_record", &ctx, &cif, &ret, &args, &nargs) != 0)
        FAIL("automatic CIF failed for make_array_record");
    if (nargs != 4 || ret->type != FFI_TYPE_STRUCT)
        FAIL("unexpected make_array_record signature");

    int a = 5, b = 6, c = 7;
    char tag = 8;
    void *values[] = { &a, &b, &c, &tag };
    IntArrayRecord result = {{0, 0, 0}, 0};
    ffi_call(&cif, FFI_FN(dlsym(handle, "make_array_record")), &result, values);
    if (result.values[0] != 5 || result.values[1] != 6 ||
        result.values[2] != 7 || result.tag != 8)
        FAIL("make_array_record result mismatch");

    free(args);
    ctf_ffi_cleanup(&ctx);
    return 0;
}

static int test_multidimensional_array(const char *path, void *handle) {
    LOG("=== TEST 3: multidimensional fixed array ===");
    ctf_ffi_context_t ctx;
    ffi_cif cif;
    ffi_type *ret = NULL, **args = NULL;
    size_t nargs = 0;
    if (build_auto(path, "sum_matrix_record", &ctx, &cif, &ret, &args, &nargs) != 0)
        FAIL("automatic CIF failed for sum_matrix_record");
    if (nargs != 1 || !ret || ret->type != FFI_TYPE_SINT64 && ret->type != FFI_TYPE_SINT32)
        FAIL("unexpected sum_matrix_record signature");

    MatrixRecord value = {{{1, 2, 3}, {4, 5, 6}}, 7};
    long result = 0;
    void *values[] = { &value };
    ffi_call(&cif, FFI_FN(dlsym(handle, "sum_matrix_record")), &result, values);
    if (result != 28)
        FAIL("sum_matrix_record result mismatch: %ld", result);

    free(args);
    ctf_ffi_cleanup(&ctx);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]);
        return 2;
    }
    void *handle = dlopen(argv[1], RTLD_NOW);
    if (!handle)
        FAIL("dlopen failed: %s", dlerror());

    int failed = test_array_member(argv[1], handle) ||
                 test_array_return(argv[1], handle) ||
                 test_multidimensional_array(argv[1], handle);
    dlclose(handle);
    LOG("RESULT: %s", failed ? "FAILED" : "ALL PHASE 3 TESTS PASSED");
    return failed;
}
