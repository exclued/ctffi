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
    if (!type) {
        LOG("%*s%s: NULL", depth * 2, "", label);
        return;
    }
    LOG("%*s%s: ptr=%p type=%u size=%zu align=%u elements=%p",
        depth * 2, "", label, (void *)type, type->type, type->size,
        type->alignment, (void *)type->elements);
    if (type->type == FFI_TYPE_STRUCT && type->elements && depth < 5) {
        for (size_t i = 0; type->elements[i]; ++i) {
            char child[32];
            snprintf(child, sizeof(child), "element[%zu]", i);
            dump_ffi_type(child, type->elements[i], depth + 1);
        }
    }
}

static void dump_struct_offsets(const char *label, ffi_type *type) {
    if (!type || type->type != FFI_TYPE_STRUCT || !type->elements)
        return;
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
    LOG("%s: cif=%p abi=%u nargs=%u bytes=%u flags=%u rtype=%p args=%p",
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
    LOG("Comparing %s", label);
    dump_ffi_type("manual", a, 1);
    dump_ffi_type("auto", b, 1);
    if (!a || !b) return a == b ? 0 : 1;
    if (a->type != b->type || a->size != b->size || a->alignment != b->alignment)
        return 1;
    if (a->type == FFI_TYPE_STRUCT) {
        size_t i = 0;
        if (!a->elements || !b->elements) return a->elements == b->elements ? 0 : 1;
        while (a->elements[i] && b->elements[i]) {
            if (compare_type("struct element", a->elements[i], b->elements[i])) return 1;
            ++i;
        }
        if (a->elements[i] || b->elements[i]) return 1;
    }
    return 0;
}

static int compare_cifs(const char *label, const ffi_cif *manual, const ffi_cif *automatic) {
    LOG("=== CIF comparison: %s ===", label);
    dump_cif("manual", manual);
    dump_cif("automatic", automatic);
    if (manual->abi != automatic->abi || manual->nargs != automatic->nargs ||
        manual->bytes != automatic->bytes || manual->flags != automatic->flags)
        return 1;
    if (compare_type("return type", manual->rtype, automatic->rtype)) return 1;
    for (unsigned i = 0; i < manual->nargs; ++i)
        if (compare_type("argument type", manual->arg_types[i], automatic->arg_types[i])) return 1;
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
    if (ctf_ffi_init(&ctx, path) != 0) FAIL("ctf_ffi_init failed");
    if (dump_ctf_function(&ctx, "point_distance") != 0) { ctf_ffi_cleanup(&ctx); return 1; }
    LOG("Building automatic CIF from CTF...");
    if (build_cif_from_ctf(&ctx, "point_distance", &automatic, &rtype, &auto_args, &auto_nargs) != 0) {
        ctf_ffi_cleanup(&ctx); FAIL("automatic CIF construction failed");
    }
    dump_cif("automatic", &automatic);

    LOG("Building reference CIF manually from { int, int }...");
    ffi_type *point_elements[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    /* For the reference representation, layout is supplied from the native C ABI.
       ffi_get_struct_offsets() is a query for libffi-prepared aggregate types and
       is not a portable way to initialize an ffi_type supplied by the caller. */
    ffi_type point = {
        FFI_TYPE_STRUCT,
        sizeof(Point2D),
        _Alignof(Point2D),
        point_elements
    };
    ffi_type *manual_args[] = { &point, &point };

    size_t native_offsets[] = { offsetof(Point2D, x), offsetof(Point2D, y) };
    LOG("manual Point2D native layout: size=%zu align=%zu offsets={%zu, %zu}",
        sizeof(Point2D), _Alignof(Point2D), native_offsets[0], native_offsets[1]);
    if (point.size != sizeof(Point2D) || point.alignment != _Alignof(Point2D) ||
        native_offsets[0] != 0 || native_offsets[1] != sizeof(int)) {
        free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual Point2D native layout is inconsistent");
    }
    if (ffi_prep_cif(&manual, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, manual_args) != FFI_OK) {
        free(auto_args); ctf_ffi_cleanup(&ctx); FAIL("manual CIF construction failed");
    }
    dump_cif("manual", &manual);
    if (auto_nargs != 2 || compare_cifs("point_distance", &manual, &automatic)) {
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
    return 0;
}

static int test_create_point(const char *path) {
    int x = 10, y = 20; void *values[] = { &x, &y };
    LOG("\n=== TEST create_point: struct return ===");
    Point2D *result = call_function_via_ctf(path, "create_point", values, 2);
    if (!result) FAIL("create_point call failed");
    LOG("create_point returned {%d, %d} (expected {10, 20})", result->x, result->y);
    if (result->x != 10 || result->y != 20) { free(result); FAIL("create_point result mismatch"); }
    free(result); return 0;
}

static int test_nested_struct(const char *path) {
    ScaledPoint input = { { 2, 3 }, 2.0 }; void *values[] = { &input };
    LOG("\n=== TEST scale_point: nested struct ===");
    ScaledPoint *result = call_function_via_ctf(path, "scale_point", values, 1);
    if (!result) FAIL("scale_point call failed");
    LOG("scale_point returned {{%d, %d}, %.3f} (expected {{4, 6}, 2.000})",
        result->origin.x, result->origin.y, result->scale);
    if (result->origin.x != 4 || result->origin.y != 6 || result->scale != 2.0) {
        free(result); FAIL("scale_point result mismatch");
    }
    free(result); return 0;
}

static int test_union(const char *path) {
    Number input = { .i = 1234 }; void *values[] = { &input };
    LOG("\n=== TEST union_int: union argument ===");
    int *result = call_function_via_ctf(path, "union_int", values, 1);
    if (!result) FAIL("union_int call failed");
    LOG("union_int returned %d (expected 1234)", *result);
    if (*result != 1234) { free(result); FAIL("union_int result mismatch"); }
    free(result); return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]); return 2; }
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s", argv[1]);
    LOG("sizeof(Point2D)=%zu align=%zu", sizeof(Point2D), _Alignof(Point2D));
    LOG("sizeof(ScaledPoint)=%zu align=%zu", sizeof(ScaledPoint), _Alignof(ScaledPoint));
    LOG("sizeof(Number)=%zu align=%zu", sizeof(Number), _Alignof(Number));
    if (test_point_distance(argv[1]) || test_create_point(argv[1]) ||
        test_nested_struct(argv[1]) || test_union(argv[1])) {
        LOG("\nRESULT: FAILED"); return 1;
    }
    LOG("\nRESULT: ALL TESTS PASSED"); return 0;
}
