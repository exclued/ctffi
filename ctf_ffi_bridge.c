/*
 * CTF-FFI Bridge: Proof of Concept Implementation
 *
 * Use CTF debug metadata to discover C function signatures and construct
 * libffi call interfaces.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <ffi.h>
#include <ctf-api.h>

#define TYPE_CACHE_SIZE 256

typedef struct {
    ctf_id_t ctf_type_id;
    ffi_type *ffi_type;
    int is_dynamic;
} type_cache_entry_t;

typedef struct {
    ctf_archive_t *arc;
    ctf_file_t *ctf;
    type_cache_entry_t type_cache[TYPE_CACHE_SIZE];
    size_t cache_count;
} ctf_ffi_context_t;

static ffi_type *ctf_to_ffi_type(ctf_ffi_context_t *ctx, ctf_id_t type_id);

int ctf_ffi_init(ctf_ffi_context_t *ctx, const char *lib_path) {
    int err;

    if (!ctx || !lib_path)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->arc = ctf_arc_open(lib_path, &err);
    if (!ctx->arc) {
        fprintf(stderr, "Failed to open CTF archive for %s (error %d)\n",
                lib_path, err);
        return -1;
    }

    ctx->ctf = ctf_arc_open_by_name(ctx->arc, NULL, &err);
    if (!ctx->ctf) {
        fprintf(stderr, "Failed to bind CTF (error %d)\n", err);
        ctf_arc_close(ctx->arc);
        ctx->arc = NULL;
        return -1;
    }

    return 0;
}

void ctf_ffi_cleanup(ctf_ffi_context_t *ctx) {
    if (!ctx)
        return;

    for (size_t i = 0; i < ctx->cache_count; ++i) {
        ffi_type *ft = ctx->type_cache[i].ffi_type;
        if (ctx->type_cache[i].is_dynamic && ft) {
            free(ft->elements);
            free(ft);
        }
    }

    if (ctx->arc)
        ctf_arc_close(ctx->arc);

    memset(ctx, 0, sizeof(*ctx));
}

static ffi_type *find_in_cache(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    for (size_t i = 0; i < ctx->cache_count; ++i)
        if (ctx->type_cache[i].ctf_type_id == type_id)
            return ctx->type_cache[i].ffi_type;
    return NULL;
}

static int add_to_cache(ctf_ffi_context_t *ctx, ctf_id_t type_id,
                        ffi_type *type, int is_dynamic) {
    if (ctx->cache_count >= TYPE_CACHE_SIZE) {
        fprintf(stderr, "CTF type cache is full\n");
        return -1;
    }

    ctx->type_cache[ctx->cache_count].ctf_type_id = type_id;
    ctx->type_cache[ctx->cache_count].ffi_type = type;
    ctx->type_cache[ctx->cache_count].is_dynamic = is_dynamic;
    ++ctx->cache_count;
    return 0;
}

static ffi_type *ctf_integer_to_ffi(ctf_file_t *ctf, ctf_id_t type_id) {
    ctf_encoding_t encoding;
    ssize_t size = ctf_type_size(ctf, type_id);

    if (size < 0 || ctf_type_encoding(ctf, type_id, &encoding) != 0) {
        fprintf(stderr, "Unable to read CTF integer encoding\n");
        return NULL;
    }

    switch (size) {
    case 0:
        return &ffi_type_void;
    case 1:
        return (encoding.cte_format & CTF_INT_SIGNED)
            ? &ffi_type_sint8 : &ffi_type_uint8;
    case 2:
        return (encoding.cte_format & CTF_INT_SIGNED)
            ? &ffi_type_sint16 : &ffi_type_uint16;
    case 4:
        return (encoding.cte_format & CTF_INT_SIGNED)
            ? &ffi_type_sint32 : &ffi_type_uint32;
    case 8:
        return (encoding.cte_format & CTF_INT_SIGNED)
            ? &ffi_type_sint64 : &ffi_type_uint64;
    default:
        fprintf(stderr, "Unsupported integer size: %zd bytes\n", size);
        return NULL;
    }
}

static ffi_type *ctf_float_to_ffi(ctf_file_t *ctf, ctf_id_t type_id) {
    ctf_encoding_t encoding;

    if (ctf_type_encoding(ctf, type_id, &encoding) != 0) {
        fprintf(stderr, "Unable to read CTF floating-point encoding\n");
        return NULL;
    }

    switch (encoding.cte_format) {
    case CTF_FP_SINGLE:
        return &ffi_type_float;
    case CTF_FP_DOUBLE:
        return &ffi_type_double;
    case CTF_FP_LDOUBLE:
        return &ffi_type_longdouble;
    default:
        fprintf(stderr, "Unsupported CTF floating-point encoding: %u\n",
                encoding.cte_format);
        return NULL;
    }
}

static ffi_type *ctf_struct_to_ffi(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    ffi_type *ffi_struct = calloc(1, sizeof(*ffi_struct));
    if (!ffi_struct)
        return NULL;

    /* Aggregate layout is deliberately left opaque until Phase 1. */
    ffi_struct->type = FFI_TYPE_STRUCT;
    ffi_struct->size = ctf_type_size(ctx->ctf, type_id);
    ffi_struct->alignment = ctf_type_align(ctx->ctf, type_id);
    ffi_struct->elements = NULL;

    if (add_to_cache(ctx, type_id, ffi_struct, 1) != 0) {
        free(ffi_struct);
        return NULL;
    }
    return ffi_struct;
}

static ffi_type *ctf_to_ffi_type(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    ffi_type *cached = find_in_cache(ctx, type_id);
    if (cached)
        return cached;

    int kind = ctf_type_kind(ctx->ctf, type_id);
    ffi_type *result = NULL;

    switch (kind) {
    case CTF_K_INTEGER:
        result = ctf_integer_to_ffi(ctx->ctf, type_id);
        break;
    case CTF_K_FLOAT:
        result = ctf_float_to_ffi(ctx->ctf, type_id);
        break;
    case CTF_K_POINTER:
    case CTF_K_FUNCTION:
        result = &ffi_type_pointer;
        break;
    case CTF_K_STRUCT:
    case CTF_K_UNION:
        result = ctf_struct_to_ffi(ctx, type_id);
        break;
    case CTF_K_ENUM:
        result = &ffi_type_sint32;
        break;
    case CTF_K_ARRAY:
        /* Function parameters are adjusted to pointers by the C language. */
        result = &ffi_type_pointer;
        break;
    default:
        fprintf(stderr, "Unsupported CTF type kind: %d\n", kind);
        break;
    }

    if (!result)
        return NULL;

    if (kind != CTF_K_STRUCT && kind != CTF_K_UNION &&
        kind != CTF_K_INTEGER && kind != CTF_K_FLOAT &&
        kind != CTF_K_POINTER && kind != CTF_K_FUNCTION &&
        kind != CTF_K_ARRAY) {
        if (add_to_cache(ctx, type_id, result, 0) != 0)
            return NULL;
    }

    return result;
}

int build_cif_from_ctf(ctf_ffi_context_t *ctx, const char *func_name,
                       ffi_cif *cif, ffi_type **rtype,
                       ffi_type ***args_out, size_t *nargs_out) {
    ctf_id_t func_type_id;
    ctf_funcinfo_t finfo;
    ctf_id_t *arg_types = NULL;
    ffi_type **args = NULL;
    size_t nargs;
    int err;

    if (!ctx || !func_name || !cif || !rtype || !args_out || !nargs_out)
        return -1;

    func_type_id = ctf_lookup_by_name(ctx->ctf, func_name);
    if (func_type_id == CTF_ERR) {
        fprintf(stderr, "Function '%s' not found in CTF\n", func_name);
        return -1;
    }

    err = ctf_func_info(ctx->ctf, func_type_id, &finfo);
    if (err != 0) {
        fprintf(stderr, "Failed to get function info for '%s'\n", func_name);
        return -1;
    }

    nargs = finfo.ctc_argc;
    if (nargs > 0) {
        arg_types = calloc(nargs, sizeof(*arg_types));
        args = calloc(nargs, sizeof(*args));
        if (!arg_types || !args) {
            fprintf(stderr, "Out of memory allocating function arguments\n");
            free(arg_types);
            free(args);
            return -1;
        }

        err = ctf_func_args(ctx->ctf, func_type_id, nargs, arg_types);
        if (err != 0) {
            fprintf(stderr, "Failed to get arguments for '%s'\n", func_name);
            free(arg_types);
            free(args);
            return -1;
        }
    }

    *rtype = ctf_to_ffi_type(ctx, finfo.ctc_return);
    if (!*rtype) {
        free(arg_types);
        free(args);
        return -1;
    }

    for (size_t i = 0; i < nargs; ++i) {
        args[i] = ctf_to_ffi_type(ctx, arg_types[i]);
        if (!args[i]) {
            fprintf(stderr, "Unsupported type for argument %zu of '%s'\n",
                    i, func_name);
            free(arg_types);
            free(args);
            return -1;
        }
    }

    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, (unsigned)nargs,
                                     *rtype, args);
    if (status != FFI_OK) {
        fprintf(stderr, "Failed to prepare CIF for '%s'\n", func_name);
        free(arg_types);
        free(args);
        return -1;
    }

    free(arg_types);
    *args_out = args;
    *nargs_out = nargs;
    return 0;
}

void *call_function_via_ctf(const char *lib_path, const char *func_name,
                            void **arg_values, size_t nargs) {
    void *handle = NULL;
    void *retval = NULL;
    void (*func)(void);
    ctf_ffi_context_t ctx;
    ffi_type *rtype = NULL;
    ffi_type **args = NULL;
    ffi_cif cif;
    size_t actual_nargs = 0;

    handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return NULL;
    }

    if (ctf_ffi_init(&ctx, lib_path) != 0)
        goto fail;

    if (build_cif_from_ctf(&ctx, func_name, &cif, &rtype, &args,
                           &actual_nargs) != 0)
        goto fail_ctx;

    if (actual_nargs != nargs) {
        fprintf(stderr, "Argument count mismatch for '%s': expected %zu, got %zu\n",
                func_name, actual_nargs, nargs);
        goto fail_ctx;
    }

    *(void **)(&func) = dlsym(handle, func_name);
    if (!func) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        goto fail_ctx;
    }

    if (rtype->size > 0) {
        retval = calloc(1, rtype->size);
        if (!retval) {
            fprintf(stderr, "Failed to allocate return value buffer\n");
            goto fail_ctx;
        }
    }

    ffi_call(&cif, FFI_FN(func), retval, arg_values);
    free(args);
    ctf_ffi_cleanup(&ctx);
    dlclose(handle);
    return retval;

fail_ctx:
    free(args);
    ctf_ffi_cleanup(&ctx);
fail:
    free(retval);
    dlclose(handle);
    return NULL;
}

int list_ctf_functions(const char *lib_path) {
    int err;
    ctf_archive_t *arc = ctf_arc_open(lib_path, &err);
    if (!arc) {
        fprintf(stderr, "Failed to open CTF archive (error %d)\n", err);
        return -1;
    }

    ctf_file_t *ctf = ctf_arc_open_by_name(arc, NULL, &err);
    if (!ctf) {
        fprintf(stderr, "Failed to bind CTF (error %d)\n", err);
        ctf_arc_close(arc);
        return -1;
    }

    ctf_next_t *iter = NULL;
    ctf_id_t id;
    printf("Functions in %s:\n", lib_path);
    while ((id = ctf_type_next(ctf, &iter, NULL, 0)) != 0) {
        if (ctf_type_kind(ctf, id) == CTF_K_FUNCTION) {
            char name_buf[256];
            char *name = ctf_type_name(ctf, id, name_buf, sizeof(name_buf));
            if (name)
                printf("  - %s\n", name);
        }
    }

    ctf_arc_close(arc);
    return 0;
}

#ifdef STANDALONE_TEST
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <library.so> <function>\n", argv[0]);
        return 1;
    }

    if (list_ctf_functions(argv[1]) != 0)
        return 1;

    printf("Calling %s\n", argv[2]);
    void *result = call_function_via_ctf(argv[1], argv[2], NULL, 0);
    if (result)
        free(result);
    return result || strcmp(argv[2], "print_status") == 0 ? 0 : 1;
}
#endif
