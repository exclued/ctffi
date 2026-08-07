#include "ctffi.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x; int y; } Point2D;
typedef struct { Point2D origin; double scale; } ScaledPoint;
typedef union { int i; double d; } Number;

#define LOG(...) do { fprintf(stderr, "[ctffi-test] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define FAIL(...) do { LOG("FAIL: " __VA_ARGS__); return 1; } while (0)

static void dump_type(const char *label, const ffi_type *t, int depth) {
    if (!t) { LOG("%*s%s: NULL", depth * 2, "", label); return; }
    LOG("%*s%s: type=%u size=%zu align=%u elements=%p", depth * 2, "", label,
        t->type, t->size, t->alignment, (void *)t->elements);
    if (t->type == FFI_TYPE_STRUCT && t->elements && depth < 6)
        for (size_t i = 0; t->elements[i]; ++i) {
            char child[64];
            snprintf(child, sizeof(child), "%s[%zu]", label, i);
            dump_type(child, t->elements[i], depth + 1);
        }
}

static int same_type(const ffi_type *a, const ffi_type *b, const char *where) {
    if (!a || !b) return a != b;
    if (a->type != b->type || a->size != b->size || a->alignment != b->alignment) {
        LOG("TYPE DIFFERENCE %s: manual={type=%u size=%zu align=%u} automatic={type=%u size=%zu align=%u}",
            where, a->type, a->size, a->alignment, b->type, b->size, b->alignment);
        return 1;
    }
    if (a->type == FFI_TYPE_STRUCT && a->elements && b->elements) {
        size_t i = 0;
        while (a->elements[i] && b->elements[i]) {
            char child[64];
            snprintf(child, sizeof(child), "%s.element[%zu]", where, i);
            if (same_type(a->elements[i], b->elements[i], child)) return 1;
            ++i;
        }
        return a->elements[i] != b->elements[i];
    }
    return (a->elements != NULL) != (b->elements != NULL);
}

static int compare_cifs(const char *name, const ffi_cif *manual, const ffi_cif *automatic) {
    LOG("--- CIF comparison: %s ---", name);
    LOG("manual:    abi=%u nargs=%u bytes=%u flags=%u", manual->abi, manual->nargs, manual->bytes, manual->flags);
    LOG("automatic: abi=%u nargs=%u bytes=%u flags=%u", automatic->abi, automatic->nargs, automatic->bytes, automatic->flags);
    dump_type("manual return", manual->rtype, 1);
    dump_type("automatic return", automatic->rtype, 1);
    int different = manual->abi != automatic->abi || manual->nargs != automatic->nargs ||
                    manual->bytes != automatic->bytes || manual->flags != automatic->flags;
    if (same_type(manual->rtype, automatic->rtype, "return")) different = 1;
    unsigned n = manual->nargs < automatic->nargs ? manual->nargs : automatic->nargs;
    for (unsigned i = 0; i < n; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "arg[%u]", i);
        dump_type("manual arg", manual->arg_types[i], 1);
        dump_type("automatic arg", automatic->arg_types[i], 1);
        if (same_type(manual->arg_types[i], automatic->arg_types[i], label)) different = 1;
    }
    LOG("CIF comparison result: %s", different ? "DIFFERENT" : "IDENTICAL");
    return different;
}

static int prepare(ffi_cif *cif, unsigned n, ffi_type *ret, ffi_type **args, const char *name) {
    ffi_status s = ffi_prep_cif(cif, FFI_DEFAULT_ABI, n, ret, args);
    LOG("ffi_prep_cif(%s): status=%d", name, s);
    return s == FFI_OK ? 0 : -1;
}

static int build_auto(const char *path, const char *name, ctf_ffi_context_t *ctx,
                      ffi_cif *cif, ffi_type **ret, ffi_type ***args, size_t *n) {
    if (ctf_ffi_init(ctx, path) != 0) return -1;
    LOG("Building automatic CIF from CTF: %s", name);
    if (build_cif_from_ctf(ctx, name, cif, ret, args, n) != 0) {
        ctf_ffi_cleanup(ctx);
        return -1;
    }
    return 0;
}

static void *symbol(void *handle, const char *name) {
    void *p = dlsym(handle, name);
    if (!p) LOG("dlsym(%s): %s", name, dlerror());
    return p;
}

static int test_scalars(const char *path) {
    LOG("\n=== TEST 1: elementary scalar types ===");
    ctf_ffi_context_t ctx;
    ffi_cif auto_cif, manual_cif;
    ffi_type *ret, **args; size_t n;
    if (build_auto(path, "add_numbers", &ctx, &auto_cif, &ret, &args, &n) != 0) FAIL("add_numbers automatic CIF failed");
    ffi_type *ma[] = { &ffi_type_sint32, &ffi_type_sint32 };
    if (prepare(&manual_cif, 2, &ffi_type_sint32, ma, "add_numbers/manual") || compare_cifs("add_numbers", &manual_cif, &auto_cif))
        { free(args); ctf_ffi_cleanup(&ctx); FAIL("add_numbers CIF mismatch"); }
    void *h = dlopen(path, RTLD_NOW); if (!h) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    int a = 5, b = 3, result = 0; void *v[] = { &a, &b };
    ffi_call(&auto_cif, FFI_FN(symbol(h, "add_numbers")), &result, v);
    LOG("add_numbers result=%d expected=8", result);
    dlclose(h); free(args); ctf_ffi_cleanup(&ctx);
    if (result != 8) FAIL("add_numbers result mismatch");

    if (build_auto(path, "compute", &ctx, &auto_cif, &ret, &args, &n) != 0) FAIL("compute automatic CIF failed");
    ffi_type *ca[] = { &ffi_type_double, &ffi_type_float, &ffi_type_sint32 };
    if (prepare(&manual_cif, 3, &ffi_type_double, ca, "compute/manual") || compare_cifs("compute", &manual_cif, &auto_cif))
        { free(args); ctf_ffi_cleanup(&ctx); FAIL("compute CIF mismatch"); }
    h = dlopen(path, RTLD_NOW); if (!h) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    double x = 2.5, result_d = 0; float y = 4.0f; int z = 10; void *cv[] = { &x, &y, &z };
    ffi_call(&auto_cif, FFI_FN(symbol(h, "compute")), &result_d, cv);
    LOG("compute result=%.6f expected=20.0", result_d);
    dlclose(h); free(args); ctf_ffi_cleanup(&ctx);
    if (result_d != 20.0) FAIL("compute result mismatch");
    return 0;
}

static int test_create_point(const char *path) {
    LOG("\n=== TEST 2: simple struct return ===");
    ctf_ffi_context_t ctx; ffi_cif auto_cif, manual_cif; ffi_type *ret, **args; size_t n;
    if (build_auto(path, "create_point", &ctx, &auto_cif, &ret, &args, &n) != 0) FAIL("create_point automatic CIF failed");
    ffi_type *pe[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, pe };
    ffi_type *ma[] = { &ffi_type_sint32, &ffi_type_sint32 };
    if (prepare(&manual_cif, 2, &point, ma, "create_point/manual") || compare_cifs("create_point", &manual_cif, &auto_cif))
        { free(args); ctf_ffi_cleanup(&ctx); FAIL("create_point CIF mismatch"); }
    void *h = dlopen(path, RTLD_NOW); if (!h) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    int x = 10, y = 20; Point2D result = {0}; void *v[] = { &x, &y };
    ffi_call(&auto_cif, FFI_FN(symbol(h, "create_point")), &result, v);
    LOG("create_point result={%d,%d} expected={10,20}", result.x, result.y);
    dlclose(h); free(args); ctf_ffi_cleanup(&ctx);
    if (result.x != 10 || result.y != 20) FAIL("create_point result mismatch");
    return 0;
}

static int test_point_distance(const char *path) {
    LOG("\n=== TEST 3: struct arguments ===");
    ctf_ffi_context_t ctx; ffi_cif auto_cif, manual_cif; ffi_type *ret, **args; size_t n;
    if (build_auto(path, "point_distance", &ctx, &auto_cif, &ret, &args, &n) != 0) FAIL("point_distance automatic CIF failed");
    ffi_type *pe[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, pe };
    ffi_type *ma[] = { &point, &point };
    if (prepare(&manual_cif, 2, &ffi_type_sint32, ma, "point_distance/manual") || compare_cifs("point_distance", &manual_cif, &auto_cif))
        { free(args); ctf_ffi_cleanup(&ctx); FAIL("point_distance CIF mismatch"); }
    void *h = dlopen(path, RTLD_NOW); if (!h) { free(args); ctf_ffi_cleanup(&ctx); FAIL("dlopen failed: %s", dlerror()); }
    Point2D p1={0,0}, p2={3,4}; int result=0; void *v[]={&p1,&p2};
    ffi_call(&auto_cif, FFI_FN(symbol(h, "point_distance")), &result, v);
    LOG("point_distance result=%d expected=25", result);
    dlclose(h); free(args); ctf_ffi_cleanup(&ctx);
    if (result != 25) FAIL("point_distance result mismatch");
    return 0;
}

static int test_nested_struct(const char *path) {
    LOG("\n=== TEST 4: nested struct ===");
    ctf_ffi_context_t ctx; ffi_cif auto_cif, manual_cif; ffi_type *ret, **args; size_t n;
    if (build_auto(path, "scale_point", &ctx, &auto_cif, &ret, &args, &n) != 0) FAIL("scale_point automatic CIF failed");
    ffi_type *pe[] = { &ffi_type_sint32, &ffi_type_sint32, NULL };
    ffi_type point = { sizeof(Point2D), _Alignof(Point2D), FFI_TYPE_STRUCT, pe };
    ffi_type *se[] = { &point, &ffi_type_double, NULL };
    ffi_type scaled = { sizeof(ScaledPoint), _Alignof(ScaledPoint), FFI_TYPE_STRUCT, se };
    ffi_type *ma[] = { &scaled };
    if (prepare(&manual_cif, 1, &scaled, ma, "scale_point/manual") || compare_cifs("scale_point", &manual_cif, &auto_cif))
        { free(args); ctf_ffi_cleanup(&ctx); FAIL("scale_point CIF mismatch"); }
    void *h=dlopen(path,RTLD_NOW); if(!h){free(args);ctf_ffi_cleanup(&ctx);FAIL("dlopen failed: %s",dlerror());}
    ScaledPoint in={{2,3},2.0}, result={{0,0},0}; void *v[]={&in};
    ffi_call(&auto_cif, FFI_FN(symbol(h,"scale_point")), &result, v);
    LOG("scale_point result={{%d,%d},%.3f} expected={{4,6},2.000}",result.origin.x,result.origin.y,result.scale);
    dlclose(h);free(args);ctf_ffi_cleanup(&ctx);
    if(result.origin.x!=4||result.origin.y!=6||result.scale!=2.0) FAIL("scale_point result mismatch");
    return 0;
}

static int test_union(const char *path) {
    LOG("\n=== TEST 5: union ===");
    LOG("Native Number: size=%zu align=%zu; int={%zu,%zu}; double={%zu,%zu}",
        sizeof(Number),_Alignof(Number),sizeof(int),_Alignof(int),sizeof(double),_Alignof(double));
    ctf_ffi_context_t ctx; ffi_cif auto_cif; ffi_type *ret=NULL, **args=NULL; size_t n=0;
    if (ctf_ffi_init(&ctx,path)!=0) FAIL("ctf_ffi_init failed");
    int status=build_cif_from_ctf(&ctx,"union_int",&auto_cif,&ret,&args,&n);
    LOG("build_cif_from_ctf(union_int): status=%d",status);
    if(status==CTFFI_UNSUPPORTED_UNION_ABI){
        LOG("WARNING: union_int rejected with CTFFI_UNSUPPORTED_UNION_ABI");
        LOG("WARNING: union-by-value ABI backend is deferred to a future phase");
        free(args);ctf_ffi_cleanup(&ctx);return 0;
    }
    free(args);ctf_ffi_cleanup(&ctx);
    if(status==0) FAIL("union_int unexpectedly accepted union-by-value");
    FAIL("union_int returned unexpected error %d",status);
}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s <test_ctf.so>\n",argv[0]);return 2;}
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s",argv[1]);
    LOG("ABI: int=%zu/%zu double=%zu/%zu ptr=%zu/%zu",sizeof(int),_Alignof(int),sizeof(double),_Alignof(double),sizeof(void*),_Alignof(void*));
    if(test_scalars(argv[1])||test_create_point(argv[1])||test_point_distance(argv[1])||test_nested_struct(argv[1])||test_union(argv[1])){
        LOG("RESULT: FAILED");return 1;
    }
    LOG("RESULT: ALL TESTS PASSED");
    return 0;
}
