#include "ctffi.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG(...) do { fprintf(stderr, "[ctffi-test] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); return 1; } while (0)

typedef struct { int x; int y; } Point2D;
typedef struct { Point2D origin; double scale; } ScaledPoint;
typedef union { int i; double d; } Number;

static void dump_type(const char *label, const ffi_type *type, int depth) {
    if (!type) { LOG("%*s%s: NULL", depth * 2, "", label); return; }
    LOG("%*s%s: ptr=%p type=%u size=%zu align=%u elements=%p",
        depth * 2, "", label, (const void *)type, type->type, type->size,
        type->alignment, (void *)type->elements);
    if (type->type == FFI_TYPE_STRUCT && type->elements && depth < 6) {
        for (size_t i = 0; type->elements[i]; ++i) {
            char child[64];
            snprintf(child, sizeof(child), "%s[%zu]", label, i);
            dump_type(child, type->elements[i], depth + 1);
        }
    }
}

static void dump_cif(const char *label, const ffi_cif *cif) {
    LOG("%s CIF: ptr=%p abi=%u nargs=%u bytes=%u flags=%u rtype=%p args=%p",
        label, (const void *)cif, cif->abi, cif->nargs, cif->bytes, cif->flags,
        (const void *)cif->rtype, (void *)cif->arg_types);
    dump_type("return", cif->rtype, 1);
    for (unsigned i = 0; i < cif->nargs; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "arg[%u]", i);
        dump_type(name, cif->arg_types[i], 1);
    }
}

static int same_type(const ffi_type *a, const ffi_type *b, const char *where) {
    if (!a || !b) {
        if (a != b) LOG("TYPE DIFFERENCE %s: one type is NULL", where);
        return a != b;
    }
    if (a->type != b->type || a->size != b->size || a->alignment != b->alignment) {
        LOG("TYPE DIFFERENCE %s: manual(type=%u,size=%zu,align=%u) "
            "automatic(type=%u,size=%zu,align=%u)",
            where, a->type, a->size, a->alignment,
            b->type, b->size, b->alignment);
        return 1;
    }
    if (a->type == FFI_TYPE_STRUCT) {
        size_t i = 0;
        if (!!a->elements != !!b->elements) return 1;
        if (a->elements) {
            while (a->elements[i] && b->elements[i]) {
                char child[64];
                snprintf(child, sizeof(child), "%s.element[%zu]", where, i);
                if (same_type(a->elements[i], b->elements[i], child)) return 1;
                ++i;
            }
            if (a->elements[i] || b->elements[i]) {
                LOG("TYPE DIFFERENCE %s: different element count", where);
                return 1;
            }
        }
    }
    return 0;
}

static int compare_cifs(const char *label, const ffi_cif *manual, const ffi_cif *automatic) {
    LOG("--- Comparing manual vs automatic CIF: %s ---", label);
    dump_cif("manual", manual);
    dump_cif("automatic", automatic);
    int different = 0;
    if (manual->abi != automatic->abi) { LOG("CIF DIFFERENCE abi: %u vs %u", manual->abi, automatic->abi); different = 1; }
    if (manual->nargs != automatic->nargs) { LOG("CIF DIFFERENCE nargs: %u vs %u", manual->nargs, automatic->nargs); different = 1; }
    if (manual->bytes != automatic->bytes) { LOG("CIF DIFFERENCE bytes: %u vs %u", manual->bytes, automatic->bytes); different = 1; }
    if (manual->flags != automatic->flags) { LOG("CIF DIFFERENCE flags: %u vs %u", manual->flags, automatic->flags); different = 1; }
    if (same_type(manual->rtype, automatic->rtype, "return")) different = 1;
    unsigned common = manual->nargs < automatic->nargs ? manual->nargs : automatic->nargs;
    for (unsigned i = 0; i < common; ++i) {
        char label_buf[32];
        snprintf(label_buf, sizeof(label_buf), "arg[%u]", i);
        if (same_type(manual->arg_types[i], automatic->arg_types[i], label_buf)) different = 1;
    }
    LOG("CIF comparison result: %s", different ? "DIFFERENT" : "IDENTICAL");
    return different;
}

static int prepare_cif(ffi_cif *cif, unsigned nargs, ffi_type *rtype, ffi_type **args, const char *label) {
    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, rtype, args);
    LOG("ffi_prep_cif(%s): status=%d", label, status);
    if (status != FFI_OK) return -1;
    return 0;
}

static int build_auto(const char *path, const char *name, ctf_ffi_context_t *ctx,
                      ffi_cif *automatic, ffi_type **rtype, ffi_type ***args, size_t *nargs) {
    if (ctf_ffi_init(ctx, path) != 0) return -1;
    LOG("Building automatic CIF from CTF for %s", name);
    if (build_cif_from_ctf(ctx, name, automatic, rtype, args, nargs) != 0) {
        ctf_ffi_cleanup(ctx);
        return -1;
    }
    return 0;
}

static int test_scalars(const char *path) {
    LOG("\n=== TEST 1: elementary scalar types ===");
    ctf_ffi_context_t ctx;
    ffi_cif add_auto, add_manual;
    ffi_type *add_rtype, **add_args; size_t add_nargs;
    if (build_auto(path, "add_numbers", &ctx, &add_auto, &add_rtype, &add_args, &add_nargs) != 0)
        FAIL("automatic add_numbers CIF construction failed");
    ffi_type *add_manual_args[] = { &ffi_type_sint32, &ffi_type_sint32 };
    if (prepare_cif(&add_manual, 2, &ffi_type_sint32, add_manual_args, "add_numbers/manual") != 0 ||
        compare_cifs("add_numbers", &add_manual, &add_auto)) {
        free(add_args); ctf_ffi_cleanup(&ctx); FAIL("add_numbers CIF mismatch");
    }
    int a = 5, b = 3; void *add_values[] = { &a, &b };
    int result = 0; void *handle = dlopen(path, RTLD_NOW); if (!handle) { free(add_args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*add_fn)(void) = NULL; *(void **)(&add_fn) = dlsym(handle, "add_numbers");
    ffi_call(&add_auto, FFI_FN(add_fn), &result, add_values);
    LOG("add_numbers(5, 3) = %d (expected 8)", result);
    dlclose(handle); free(add_args); ctf_ffi_cleanup(&ctx);
    if (result != 8) FAIL("add_numbers result mismatch");

    ffi_cif compute_auto, compute_manual; ffi_type *compute_rtype, **compute_args; size_t compute_nargs;
    if (build_auto(path, "compute", &ctx, &compute_auto, &compute_rtype, &compute_args, &compute_nargs) != 0)
        FAIL("automatic compute CIF construction failed");
    ffi_type *compute_manual_args[] = { &ffi_type_double, &ffi_type_float, &ffi_type_sint32 };
    if (prepare_cif(&compute_manual, 3, &ffi_type_double, compute_manual_args, "compute/manual") != 0 ||
        compare_cifs("compute", &compute_manual, &compute_auto)) {
        free(compute_args); ctf_ffi_cleanup(&ctx); FAIL("compute CIF mismatch");
    }
    double x = 2.5; float y = 4.0f; int z = 10; void *values[] = { &x, &y, &z }; double computed = 0;
    handle = dlopen(path, RTLD_NOW); if (!handle) { free(compute_args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*compute_fn)(void) = NULL; *(void **)(&compute_fn) = dlsym(handle, "compute");
    ffi_call(&compute_auto, FFI_FN(compute_fn), &computed, values);
    LOG("compute(2.5, 4.0, 10) = %.6f (expected 20.0)", computed);
    dlclose(handle); free(compute_args); ctf_ffi_cleanup(&ctx);
    if (computed != 20.0) FAIL("compute result mismatch");
    return 0;
}

static int test_create_point(const char *path) {
    LOG("\n=== TEST 2: simple struct return ===");
    ctf_ffi_context_t ctx; ffi_cif automatic, manual; ffi_type *rtype, **args; size_t nargs;
    if (build_auto(path, "create_point", &ctx, &automatic, &rtype, &args, &nargs) != 0)
        FAIL("automatic create_point CIF construction failed");
    ffi_type *point_elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, point_elements };
    ffi_type *manual_args[] = { &ffi_type_sint32, &ffi_type_sint32 };
    if (prepare_cif(&manual, 2, &point, manual_args, "create_point/manual") != 0 ||
        compare_cifs("create_point", &manual, &automatic)) {
        free(args); ctf_ffi_cleanup(&ctx); FAIL("create_point CIF mismatch");
    }
    int x = 10, y = 20; void *values[] = { &x, &y }; Point2D result;
    void *handle = dlopen(path, RTLD_NOW); if (!handle) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL; *(void **)(&fn) = dlsym(handle, "create_point");
    memset(&result, 0, sizeof(result)); ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("create_point returned {%d, %d} (expected {10, 20})", result.x, result.y);
    dlclose(handle); free(args); ctf_ffi_cleanup(&ctx);
    if (result.x != 10 || result.y != 20) FAIL("create_point result mismatch");
    return 0;
}

static int test_point_distance(const char *path) {
    LOG("\n=== TEST 3: struct arguments ===");
    ctf_ffi_context_t ctx; ffi_cif automatic, manual; ffi_type *rtype, **args; size_t nargs;
    if (build_auto(path, "point_distance", &ctx, &automatic, &rtype, &args, &nargs) != 0)
        FAIL("automatic point_distance CIF construction failed");
    ffi_type *elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, elements };
    ffi_type *manual_args[] = { &point, &point };
    if (prepare_cif(&manual, 2, &ffi_type_sint32, manual_args, "point_distance/manual") != 0 ||
        compare_cifs("point_distance", &manual, &automatic)) {
        free(args); ctf_ffi_cleanup(&ctx); FAIL("point_distance CIF mismatch");
    }
    Point2D p1 = {0, 0}, p2 = {3, 4}; void *values[] = { &p1, &p2 }; int result = 0;
    void *handle = dlopen(path, RTLD_NOW); if (!handle) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL; *(void **)(&fn) = dlsym(handle, "point_distance");
    ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("point_distance returned %d (expected 25)", result);
    dlclose(handle); free(args); ctf_ffi_cleanup(&ctx);
    if (result != 25) FAIL("point_distance result mismatch");
    return 0;
}

static int test_nested_struct(const char *path) {
    LOG("\n=== TEST 4: nested struct ===");
    ctf_ffi_context_t ctx; ffi_cif automatic, manual; ffi_type *rtype, **args; size_t nargs;
    if (build_auto(path, "scale_point", &ctx, &automatic, &rtype, &args, &nargs) != 0)
        FAIL("automatic scale_point CIF construction failed");
    ffi_type *point_elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, point_elements };
    ffi_type *scaled_elements[] = { &point, &ffi_type_double, NULL };
    ffi_type scaled = { sizeof(ScaledPoint), _Alignof(ScaledPoint), FFI_TYPE_STRUCT, scaled_elements };
    ffi_type *manual_args[] = { &scaled };
    if (prepare_cif(&manual, 1, &scaled, manual_args, "scale_point/manual") != 0 ||
        compare_cifs("scale_point", &manual, &automatic)) {
        free(args); ctf_ffi_cleanup(&ctx); FAIL("scale_point CIF mismatch");
    }
    ScaledPoint input = {{2, 3}, 2.0}, result; void *values[] = { &input };
    void *handle = dlopen(path, RTLD_NOW); if (!handle) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL; *(void **)(&fn) = dlsym(handle, "scale_point");
    memset(&result, 0, sizeof(result)); ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("scale_point returned {{%d, %d}, %.3f} (expected {{4, 6}, 2.000})", result.origin.x, result.origin.y, result.scale);
    dlclose(handle); free(args); ctf_ffi_cleanup(&ctx);
    if (result.origin.x != 4 || result.origin.y != 6 || result.scale != 2.0) FAIL("scale_point result mismatch");
    return 0;
}

static int test_union(const char *path) {
    LOG("\n=== TEST 5: union ===");
    LOG("Native Number: size=%zu align=%zu; int={size=%zu align=%zu}; double={size=%zu align=%zu}",
        sizeof(Number), _Alignof(Number), sizeof(int), _Alignof(int), sizeof(double), _Alignof(double));
    ctf_ffi_context_t ctx; ffi_cif automatic, manual; ffi_type *rtype, **args; size_t nargs;
    if (build_auto(path, "union_int", &ctx, &automatic, &rtype, &args, &nargs) != 0)
        FAIL("automatic union_int CIF construction failed");

    ffi_type *manual_elements[] = { &ffi_type_double, NULL };
    ffi_type manual_union = { sizeof(Number), _Alignof(Number), FFI_TYPE_STRUCT, manual_elements };
    ffi_type *manual_args[] = { &manual_union };
    LOG("Reference union emulation: single largest member = double");
    if (prepare_cif(&manual, 1, &ffi_type_sint32, manual_args, "union_int/manual") != 0) {
        free(args); ctf_ffi_cleanup(&ctx); FAIL("manual union CIF construction failed");
    }
    if (compare_cifs("union_int", &manual, &automatic)) {
        LOG("Union CIF mismatch is diagnostic: libffi has no native union type; "
            "the FFI_TYPE_STRUCT emulation must match the target ABI");
        free(args); ctf_ffi_cleanup(&ctx); FAIL("union manual and automatic CIF differ");
    }

    Number input; memset(&input, 0, sizeof(input)); input.i = 1234;
    LOG("Input bytes:");
    const unsigned char *bytes = (const unsigned char *)&input;
    for (size_t i = 0; i < sizeof(input); ++i) fprintf(stderr, "%02x%s", bytes[i], i + 1 == sizeof(input) ? "\n" : " ");
    void *values[] = { &input }; int result = 0;
    void *handle = dlopen(path, RTLD_NOW); if (!handle) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL; *(void **)(&fn) = dlsym(handle, "union_int");
    LOG("Calling union_int through automatic CIF...");
    ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("union_int returned %d (expected 1234)", result);
    dlclose(handle); free(args); ctf_ffi_cleanup(&ctx);
    if (result != 1234) FAIL("union_int result mismatch");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]); return 2; }
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s", argv[1]);
    LOG("ABI: sizeof(int)=%zu align(int)=%zu sizeof(double)=%zu align(double)=%zu sizeof(void*)=%zu",
        sizeof(int), _Alignof(int), sizeof(double), _Alignof(double), sizeof(void *));
    if (test_scalars(argv[1]) || test_create_point(argv[1]) || test_point_distance(argv[1]) ||
        test_nested_struct(argv[1]) || test_union(argv[1])) {
        LOG("\nRESULT: FAILED");
        return 1;
    }
    LOG("\nRESULT: ALL TESTS PASSED");
    return 0;
}
