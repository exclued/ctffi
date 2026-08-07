#include "ctffi.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) do { fprintf(stderr, "[ctffi-test] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); return 1; } while (0)

typedef struct { int x; int y; } Point2D;
typedef struct { Point2D origin; double scale; } ScaledPoint;
typedef union { int i; double d; } Number;

static int test_scalars(const char *path) {
    LOG("\n=== TEST 1: elementary scalar types ===");

    int a = 5, b = 3;
    void *add_values[] = { &a, &b };
    int *sum = call_function_via_ctf(path, "add_numbers", add_values, 2);
    if (!sum) FAIL("add_numbers call failed");
    LOG("add_numbers(5, 3) = %d (expected 8)", *sum);
    if (*sum != 8) { free(sum); FAIL("add_numbers result mismatch"); }
    free(sum);

    double x = 2.5;
    float y = 4.0f;
    int z = 10;
    void *compute_values[] = { &x, &y, &z };
    double *computed = call_function_via_ctf(path, "compute", compute_values, 3);
    if (!computed) FAIL("compute call failed");
    LOG("compute(2.5, 4.0, 10) = %.6f (expected 20.0)", *computed);
    if (*computed != 20.0) { free(computed); FAIL("compute result mismatch"); }
    free(computed);
    return 0;
}

static int test_create_point(const char *path) {
    int x = 10, y = 20;
    void *values[] = { &x, &y };
    LOG("\n=== TEST 2: simple struct return ===");
    Point2D *result = call_function_via_ctf(path, "create_point", values, 2);
    if (!result) FAIL("create_point call failed");
    LOG("create_point returned {%d, %d} (expected {10, 20})", result->x, result->y);
    if (result->x != 10 || result->y != 20) { free(result); FAIL("create_point result mismatch"); }
    free(result);
    return 0;
}

static int test_point_distance(const char *path) {
    ctf_ffi_context_t ctx;
    ffi_cif automatic, manual;
    ffi_type *rtype = NULL, **auto_args = NULL;
    size_t auto_nargs = 0;
    LOG("\n=== TEST 3: struct arguments and CIF comparison ===");
    LOG("Native Point2D: sizeof=%zu align=%zu offsetof(x)=%zu offsetof(y)=%zu",
        sizeof(Point2D), _Alignof(Point2D), offsetof(Point2D, x), offsetof(Point2D, y));
    if (ctf_ffi_init(&ctx, path) != 0) FAIL("ctf_ffi_init failed");
    if (build_cif_from_ctf(&ctx, "point_distance", &automatic, &rtype, &auto_args, &auto_nargs) != 0) {
        ctf_ffi_cleanup(&ctx); FAIL("automatic CIF construction failed");
    }

    ffi_type *point_elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, point_elements };
    ffi_type *manual_args[] = { &point, &point };
    if (ffi_prep_cif(&manual, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, manual_args) != FFI_OK) {
        free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual CIF construction failed");
    }
    LOG("manual CIF: abi=%u nargs=%u bytes=%u flags=%u", manual.abi, manual.nargs, manual.bytes, manual.flags);
    LOG("auto CIF:   abi=%u nargs=%u bytes=%u flags=%u", automatic.abi, automatic.nargs, automatic.bytes, automatic.flags);
    if (manual.abi != automatic.abi || manual.nargs != automatic.nargs ||
        manual.bytes != automatic.bytes || manual.flags != automatic.flags ||
        point.size != automatic.arg_types[0]->size || point.alignment != automatic.arg_types[0]->alignment) {
        free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual and automatic Point2D CIF/type differ");
    }

    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) { free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL;
    *(void **)(&fn) = dlsym(handle, "point_distance");
    if (!fn) { dlclose(handle); free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("dlsym failed: %s", dlerror()); }
    Point2D p1 = { 0, 0 }, p2 = { 3, 4 };
    void *values[] = { &p1, &p2 };
    int result = 0;
    ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("point_distance returned %d (expected 25)", result);
    dlclose(handle); free(auto_args); ctf_ffi_cleanup(&ctx);
    if (result != 25) FAIL("point_distance result mismatch");
    return 0;
}

static int test_nested_struct(const char *path) {
    ScaledPoint input = { { 2, 3 }, 2.0 };
    void *values[] = { &input };
    LOG("\n=== TEST 4: nested struct ===");
    ScaledPoint *result = call_function_via_ctf(path, "scale_point", values, 1);
    if (!result) FAIL("scale_point call failed");
    LOG("scale_point returned {{%d, %d}, %.3f} (expected {{4, 6}, 2.000})",
        result->origin.x, result->origin.y, result->scale);
    if (result->origin.x != 4 || result->origin.y != 6 || result->scale != 2.0) {
        free(result); FAIL("scale_point result mismatch");
    }
    free(result);
    return 0;
}

static int test_union(const char *path) {
    Number input = { .i = 1234 };
    void *values[] = { &input };
    LOG("\n=== TEST 5: union ===");
    LOG("Native Number: sizeof=%zu align=%zu; int member size=%zu align=%zu; double member size=%zu align=%zu",
        sizeof(Number), _Alignof(Number), sizeof(input.i), _Alignof(int), sizeof(input.d), _Alignof(double));
    int *result = call_function_via_ctf(path, "union_int", values, 1);
    if (!result) FAIL("union_int call failed");
    LOG("union_int returned %d (expected 1234)", *result);
    if (*result != 1234) { free(result); FAIL("union_int result mismatch"); }
    free(result);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]); return 2; }
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s", argv[1]);
    LOG("ABI: sizeof(int)=%zu align(int)=%zu sizeof(double)=%zu align(double)=%zu",
        sizeof(int), _Alignof(int), sizeof(double), _Alignof(double));

    if (test_scalars(argv[1]) ||
        test_create_point(argv[1]) ||
        test_point_distance(argv[1]) ||
        test_nested_struct(argv[1]) ||
        test_union(argv[1])) {
        LOG("\nRESULT: FAILED");
        return 1;
    }
    LOG("\nRESULT: ALL TESTS PASSED");
    return 0;
}
