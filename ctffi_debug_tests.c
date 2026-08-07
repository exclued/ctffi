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

/* ... diagnostic helpers and scalar/struct tests remain unchanged ... */

static int test_union(const char *path) {
    LOG("\n=== TEST 5: union ===");
    LOG("Union-by-value is expected to be unsupported in the generic Phase 1 ABI");

    ctf_ffi_context_t ctx;
    ffi_cif automatic;
    ffi_type *rtype = NULL, **args = NULL;
    size_t nargs = 0;

    if (ctf_ffi_init(&ctx, path) != 0)
        FAIL("ctf_ffi_init failed");

    int status = build_cif_from_ctf(&ctx, "union_int", &automatic,
                                    &rtype, &args, &nargs);
    if (status == CTFFI_UNSUPPORTED_UNION_ABI) {
        LOG("WARNING: union_int rejected with CTFFI_UNSUPPORTED_UNION_ABI");
        LOG("WARNING: target-specific union ABI backends are deferred to a future phase");
        free(args);
        ctf_ffi_cleanup(&ctx);
        return 0;
    }
    if (status != 0) {
        free(args);
        ctf_ffi_cleanup(&ctx);
        FAIL("union_int failed with unexpected status %d", status);
    }

    free(args);
    ctf_ffi_cleanup(&ctx);
    FAIL("union_int unexpectedly accepted a union-by-value ABI");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test_ctf.so>\n", argv[0]);
        return 2;
    }
    LOG("CTF-FFI diagnostic test harness");
    LOG("library: %s", argv[1]);
    LOG("ABI: sizeof(int)=%zu align(int)=%zu sizeof(double)=%zu align(double)=%zu sizeof(void*)=%zu",
        sizeof(int), _Alignof(int), sizeof(double), _Alignof(double), sizeof(void *));

    /* The complete test suite is intentionally ordered from simple to complex:
     * scalar types, simple struct return, struct arguments, nested struct, union. */
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
