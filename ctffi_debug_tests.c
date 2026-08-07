#include "ctffi.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG(...) do { fprintf(stderr, "[ctffi-test] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: " __VA_ARGS__); return 1; } while (0)
#define CHECK_EQ(label, actual, expected) \
    do { \
        unsigned long long _a = (unsigned long long)(actual); \
        unsigned long long _e = (unsigned long long)(expected); \
        LOG("CHECK %-32s actual=%llu expected=%llu %s", label, _a, _e, _a == _e ? "OK" : "FAIL"); \
        if (_a != _e) return 1; \
    } while (0)

typedef struct { int x; int y; } Point2D;
typedef struct { Point2D origin; double scale; } ScaledPoint;
typedef union { int i; double d; } Number;

static const char *kind_name(int kind) {
    switch (kind) {
    case CTF_K_INTEGER: return "integer";
    case CTF_K_FLOAT: return "float";
    case CTF_K_POINTER: return "pointer";
    case CTF_K_FUNCTION: return "function";
    case CTF_K_STRUCT: return "struct";
    case CTF_K_UNION: return "union";
    case CTF_K_ENUM: return "enum";
    case CTF_K_ARRAY: return "array";
    case CTF_K_TYPEDEF: return "typedef";
    default: return "other";
    }
}

static void dump_ffi_type(const char *label, ffi_type *type, int depth) {
    if (!type) { LOG("%*s%s: NULL", depth * 2, "", label); return; }
    LOG("%*s%s: ptr=%p type=%u size=%zu align=%u elements=%p",
        depth * 2, "", label, (void *)type, type->type, type->size,
        type->alignment, (void *)type->elements);
    if (type->type == FFI_TYPE_STRUCT && type->elements && depth < 8) {
        for (size_t i = 0; type->elements[i]; ++i) {
            char child[32];
            snprintf(child, sizeof(child), "element[%zu]", i);
            dump_ffi_type(child, type->elements[i], depth + 1);
        }
    }
}

static void dump_struct_offsets(const char *label, ffi_type *type) {
    if (!type || type->type != FFI_TYPE_STRUCT || !type->elements) return;
    size_t count = 0;
    while (type->elements[count]) ++count;
    size_t *offsets = calloc(count ? count : 1, sizeof(*offsets));
    if (!offsets) { LOG("%s offsets: allocation failed", label); return; }
    ffi_status status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, type, offsets);
    LOG("%s offsets: status=%d", label, status);
    if (status == FFI_OK)
        for (size_t i = 0; i < count; ++i)
            LOG("  %s[%zu] offset=%zu", label, i, offsets[i]);
    free(offsets);
}

static void dump_cif(const char *label, const ffi_cif *cif) {
    LOG("%s CIF: cif=%p abi=%u nargs=%u bytes=%u flags=%u rtype=%p args=%p",
        label, (void *)cif, cif->abi, cif->nargs, cif->bytes, cif->flags,
        (void *)cif->rtype, (void *)cif->arg_types);
    dump_ffi_type("return", cif->rtype, 1);
    dump_struct_offsets("return", cif->rtype);
    for (unsigned i = 0; i < cif->nargs; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "arg[%u]", i);
        dump_ffi_type(name, cif->arg_types[i], 1);
        dump_struct_offsets(name, cif->arg_types[i]);
    }
}

static int compare_type(const char *label, ffi_type *a, ffi_type *b) {
    LOG("--- comparing %s ---", label);
    dump_ffi_type("manual", a, 1);
    dump_ffi_type("automatic", b, 1);
    if (!a || !b) {
        LOG("type pointer presence: %s", a == b ? "OK" : "FAIL");
        return a == b ? 0 : 1;
    }
    if (a->type != b->type) { LOG("type mismatch: manual=%u automatic=%u", a->type, b->type); return 1; }
    if (a->size != b->size) { LOG("size mismatch: manual=%zu automatic=%zu", a->size, b->size); return 1; }
    if (a->alignment != b->alignment) { LOG("alignment mismatch: manual=%u automatic=%u", a->alignment, b->alignment); return 1; }
    if (a->type == FFI_TYPE_STRUCT) {
        size_t i = 0;
        if (!a->elements || !b->elements) return a->elements == b->elements ? 0 : 1;
        while (a->elements[i] && b->elements[i]) {
            char child[64];
            snprintf(child, sizeof(child), "%s.element[%zu]", label, i);
            if (compare_type(child, a->elements[i], b->elements[i])) return 1;
            ++i;
        }
        if (a->elements[i] || b->elements[i]) {
            LOG("element count mismatch at %s", label);
            return 1;
        }
    }
    LOG("%s: types match", label);
    return 0;
}

static int compare_cifs(const char *label, const ffi_cif *manual, const ffi_cif *automatic) {
    LOG("=== CIF comparison: %s ===", label);
    dump_cif("manual", manual);
    dump_cif("automatic", automatic);
    CHECK_EQ("CIF abi", manual->abi, automatic->abi);
    CHECK_EQ("CIF nargs", manual->nargs, automatic->nargs);
    CHECK_EQ("CIF bytes", manual->bytes, automatic->bytes);
    CHECK_EQ("CIF flags", manual->flags, automatic->flags);
    if (compare_type("return type", manual->rtype, automatic->rtype)) return 1;
    for (unsigned i = 0; i < manual->nargs; ++i) {
        char label_arg[64];
        snprintf(label_arg, sizeof(label_arg), "argument[%u]", i);
        if (compare_type(label_arg, manual->arg_types[i], automatic->arg_types[i])) return 1;
    }
    LOG("CIF comparison: PASS");
    return 0;
}

static int dump_ctf_function(ctf_ffi_context_t *ctx, const char *name) {
    ctf_id_t fn = ctf_lookup_by_symbol_name(ctx->ctf, name);
    ctf_funcinfo_t info;
    if (fn == CTF_ERR || ctf_func_type_info(ctx->ctf, fn, &info) != 0)
        FAIL("cannot inspect CTF function %s", name);
    LOG("=== CTF function: %s ===", name);
    LOG("function type id=%lu return=%lu argc=%u flags=%u", (unsigned long)fn,
        (unsigned long)info.ctc_return, info.ctc_argc, info.ctc_flags);
    if (info.ctc_argc) {
        ctf_id_t *args = calloc(info.ctc_argc, sizeof(*args));
        if (!args) FAIL("out of memory inspecting %s", name);
        if (ctf_func_type_args(ctx->ctf, fn, info.ctc_argc, args) != 0) {
            free(args); FAIL("cannot inspect arguments of %s", name);
        }
        for (unsigned i = 0; i < info.ctc_argc; ++i) {
            char type_name[256];
            const char *name_result = ctf_type_name(ctx->ctf, args[i], type_name, sizeof(type_name));
            ssize_t size = ctf_type_size(ctx->ctf, args[i]);
            int align = ctf_type_align(ctx->ctf, args[i]);
            LOG("arg[%u]: id=%lu kind=%s name=%s size=%zd align=%d", i,
                (unsigned long)args[i], kind_name(ctf_type_kind(ctx->ctf, args[i])),
                name_result ? name_result : "<anonymous>", size, align);
        }
        free(args);
    }
    return 0;
}

static int test_point_distance(const char *path) {
    ctf_ffi_context_t ctx;
    ffi_cif automatic, manual;
    ffi_type *rtype = NULL, **auto_args = NULL;
    size_t auto_nargs = 0;
    LOG("\n=== TEST point_distance: struct arguments ===");
    LOG("Native Point2D: sizeof=%zu align=%zu offsetof(x)=%zu offsetof(y)=%zu sizeof(int)=%zu align(int)=%zu",
        sizeof(Point2D), _Alignof(Point2D), offsetof(Point2D, x), offsetof(Point2D, y), sizeof(int), _Alignof(int));
    if (ctf_ffi_init(&ctx, path) != 0) FAIL("ctf_ffi_init failed");
    if (dump_ctf_function(&ctx, "point_distance") != 0) { ctf_ffi_cleanup(&ctx); return 1; }
    LOG("Building automatic CIF from CTF...");
    if (build_cif_from_ctf(&ctx, "point_distance", &automatic, &rtype, &auto_args, &auto_nargs) != 0) {
        ctf_ffi_cleanup(&ctx); FAIL("automatic CIF construction failed");
    }
    dump_cif("automatic", &automatic);

    LOG("Building reference CIF manually from { int, int }...");
    ffi_type *point_elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { FFI_TYPE_STRUCT, sizeof(Point2D), _Alignof(Point2D), point_elements };
    ffi_type *manual_args[] = { &point, &point };
    LOG("Manual ffi_type Point2D: ptr=%p type=%u size=%zu align=%u elements=%p",
        (void *)&point, point.type, point.size, point.alignment, (void *)point.elements);
    LOG("Manual element[0]: ptr=%p type=%u size=%zu align=%u native_offset=%zu",
        (void *)&ffi_type_sint32, ffi_type_sint32.type, ffi_type_sint32.size,
        ffi_type_sint32.alignment, offsetof(Point2D, x));
    LOG("Manual element[1]: ptr=%p type=%u size=%zu align=%u native_offset=%zu",
        (void *)&ffi_type_sint32, ffi_type_sint32.type, ffi_type_sint32.size,
        ffi_type_sint32.alignment, offsetof(Point2D, y));
    LOG("Manual Point2D expected: size=%zu align=%zu offsets={%zu,%zu}",
        sizeof(Point2D), _Alignof(Point2D), offsetof(Point2D, x), offsetof(Point2D, y));
    CHECK_EQ("manual struct size", point.size, sizeof(Point2D));
    CHECK_EQ("manual struct align", point.alignment, _Alignof(Point2D));
    CHECK_EQ("manual field x offset", offsetof(Point2D, x), 0);
    CHECK_EQ("manual field y offset", offsetof(Point2D, y), sizeof(int));
    CHECK_EQ("manual field count", 2, 2);

    LOG("Calling ffi_prep_cif() for manual CIF...");
    ffi_status manual_status = ffi_prep_cif(&manual, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, manual_args);
    LOG("ffi_prep_cif(manual): status=%d", manual_status);
    if (manual_status != FFI_OK) { free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual CIF construction failed"); }
    dump_cif("manual", &manual);
    if (auto_nargs != 2) { free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("automatic nargs=%zu, expected 2", auto_nargs); }
    if (compare_cifs("point_distance", &manual, &automatic)) {
        free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual and automatic CIF differ");
    }

    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) { free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    void (*fn)(void) = NULL;
    *(void **)(&fn) = dlsym(handle, "point_distance");
    if (!fn) { dlclose(handle); free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("dlsym failed: %s", dlerror()); }
    Point2D p1 = { 0, 0 }, p2 = { 3, 4 };
    void *values[] = { &p1, &p2 };
    int result = 0;
    LOG("Calling point_distance through automatic CIF...");
    ffi_call(&automatic, FFI_FN(fn), &result, values);
    LOG("point_distance returned %d (expected 25)", result);
    dlclose(handle); free(auto_args); ctf_ffi_cleanup(&ctx);
    if (result != 25) FAIL("point_distance returned %d", result);
    LOG("point_distance test: PASS");
    return 0;
}

static int test_create_point(const char *path) {
    int x = 10, y = 20; void *values[] = { &x, &y };
    LOG("\n=== TEST create_point: struct return ===");
    Point2D *result = call_function_via_ctf(path, "create_point", values, 2);
    if (!result) FAIL("create_point call failed");
    LOG("create_point returned {%d, %d} (expected {10, 20})", result->x, result->y);
    if (result->x != 10 || result->y != 20) { free(result); FAIL("create_point result mismatch"); }
    free(result); LOG("create_point test: PASS"); return 0;
}

static int test_nested_struct(const char *path) {
    ScaledPoint input = { { 2, 3 }, 2.0 }; void *values[] = { &input };
    LOG("\n=== TEST scale_point: nested struct ===");
    Point2D expected_origin = { 4, 6 };
    LOG("Input: origin={%d,%d} scale=%.17g", input.origin.x, input.origin.y, input.scale);
    LOG("Expected: origin={%d,%d} scale=%.17g", expected_origin.x, expected_origin.y, input.scale);
    ScaledPoint *result = call_function_via_ctf(path, "scale_point", values, 1);
    if (!result) FAIL("scale_point call failed");
    LOG("scale_point returned {{%d, %d}, %.17g}", result->origin.x, result->origin.y, result->scale);
    if (result->origin.x != 4 || result->origin.y != 6 || result->scale != 2.0) {
        free(result); FAIL("scale_point result mismatch");
    }
    free(result); LOG("scale_point test: PASS"); return 0;
}

static int test_union(const char *path) {
    Number input = { .i = 1234 }; void *values[] = { &input };
    LOG("\n=== TEST union_int: union argument ===");
    LOG("Native Number: sizeof=%zu align=%zu; input.i=%d", sizeof(Number), _Alignof(Number), input.i);
    int *result = call_function_via_ctf(path, "union_int", values, 1);
    if (!result) FAIL("union_int call failed");
    LOG("union_int returned %d (expected 1234)", *result);
    if (*result != 1234) { free(result); FAIL("union_int result mismatch"); }
    free(result); LOG("union_int test: PASS"); return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]); return 2; }
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s", argv[1]);
    LOG("compiler ABI: sizeof(int)=%zu align(int)=%zu sizeof(double)=%zu align(double)=%zu",
        sizeof(int), _Alignof(int), sizeof(double), _Alignof(double));
    LOG("sizeof(Point2D)=%zu align=%zu", sizeof(Point2D), _Alignof(Point2D));
    LOG("sizeof(ScaledPoint)=%zu align=%zu", sizeof(ScaledPoint), _Alignof(ScaledPoint));
    LOG("sizeof(Number)=%zu align=%zu", sizeof(Number), _Alignof(Number));
    if (test_point_distance(argv[1]) || test_create_point(argv[1]) ||
        test_nested_struct(argv[1]) || test_union(argv[1])) {
        LOG("\nRESULT: FAILED"); return 1;
    }
    LOG("\nRESULT: ALL TESTS PASSED"); return 0;
}
