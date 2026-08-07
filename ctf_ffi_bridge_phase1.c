#include <ctf-api.h>
#include <dlfcn.h>
#include <ffi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_CACHE_SIZE 256

typedef struct {
    ctf_id_t id;
    ffi_type *type;
    int dynamic;
} type_cache_entry_t;

typedef struct {
    ctf_archive_t *archive;
    ctf_file_t *ctf;
    type_cache_entry_t cache[TYPE_CACHE_SIZE];
    size_t cache_count;
} ctf_ffi_context_t;

typedef struct {
    ctf_ffi_context_t *ctx;
    ffi_type **elements;
    size_t count;
    size_t capacity;
    int error;
} member_builder_t;

static ffi_type *ctf_to_ffi_type(ctf_ffi_context_t *, ctf_id_t);

static ffi_type *cache_find(ctf_ffi_context_t *ctx, ctf_id_t id) {
    for (size_t i = 0; i < ctx->cache_count; ++i)
        if (ctx->cache[i].id == id)
            return ctx->cache[i].type;
    return NULL;
}

static int cache_add(ctf_ffi_context_t *ctx, ctf_id_t id, ffi_type *type,
                     int dynamic) {
    if (ctx->cache_count == TYPE_CACHE_SIZE)
        return -1;
    ctx->cache[ctx->cache_count++] =
        (type_cache_entry_t){ id, type, dynamic };
    return 0;
}

int ctf_ffi_init(ctf_ffi_context_t *ctx, const char *path) {
    int err;
    memset(ctx, 0, sizeof(*ctx));
    ctx->archive = ctf_arc_open(path, &err);
    if (!ctx->archive)
        return -1;
    ctx->ctf = ctf_arc_open_by_name(ctx->archive, NULL, &err);
    if (!ctx->ctf) {
        ctf_arc_close(ctx->archive);
        ctx->archive = NULL;
        return -1;
    }
    return 0;
}

void ctf_ffi_cleanup(ctf_ffi_context_t *ctx) {
    if (!ctx)
        return;
    for (size_t i = 0; i < ctx->cache_count; ++i) {
        if (ctx->cache[i].dynamic) {
            free(ctx->cache[i].type->elements);
            free(ctx->cache[i].type);
        }
    }
    if (ctx->archive)
        ctf_arc_close(ctx->archive);
    memset(ctx, 0, sizeof(*ctx));
}

static ffi_type *integer_type(ctf_file_t *ctf, ctf_id_t id) {
    ctf_encoding_t enc;
    ssize_t size = ctf_type_size(ctf, id);
    if (size < 0 || ctf_type_encoding(ctf, id, &enc) != 0)
        return NULL;
    switch (size) {
    case 1: return (enc.cte_format & CTF_INT_SIGNED) ? &ffi_type_sint8 : &ffi_type_uint8;
    case 2: return (enc.cte_format & CTF_INT_SIGNED) ? &ffi_type_sint16 : &ffi_type_uint16;
    case 4: return (enc.cte_format & CTF_INT_SIGNED) ? &ffi_type_sint32 : &ffi_type_uint32;
    case 8: return (enc.cte_format & CTF_INT_SIGNED) ? &ffi_type_sint64 : &ffi_type_uint64;
    default: return NULL;
    }
}

static ffi_type *float_type(ctf_file_t *ctf, ctf_id_t id) {
    ctf_encoding_t enc;
    if (ctf_type_encoding(ctf, id, &enc) != 0)
        return NULL;
    switch (enc.cte_format) {
    case CTF_FP_SINGLE: return &ffi_type_float;
    case CTF_FP_DOUBLE: return &ffi_type_double;
    case CTF_FP_LDOUBLE: return &ffi_type_longdouble;
    default: return NULL;
    }
}

static int append_member(member_builder_t *b, ffi_type *type) {
    if (b->count == b->capacity) {
        size_t n = b->capacity ? b->capacity * 2 : 4;
        ffi_type **p = realloc(b->elements, n * sizeof(*p));
        if (!p)
            return -1;
        b->elements = p;
        b->capacity = n;
    }
    b->elements[b->count++] = type;
    return 0;
}

static int member_cb(const char *name, ctf_id_t member_type,
                     unsigned long offset, void *arg) {
    member_builder_t *b = arg;
    (void)name;
    if (offset % 8 != 0) {
        b->error = 1;
        return 1;
    }
    ffi_type *type = ctf_to_ffi_type(b->ctx, member_type);
    if (!type || append_member(b, type) != 0) {
        b->error = 1;
        return 1;
    }
    return 0;
}

static int layout_struct(ffi_type *type) {
    ffi_cif cif;
    return ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, type, NULL) == FFI_OK ? 0 : -1;
}

static ffi_type *aggregate_type(ctf_ffi_context_t *ctx, ctf_id_t id,
                                int is_union) {
    ffi_type *result = calloc(1, sizeof(*result));
    member_builder_t b = { .ctx = ctx };
    if (!result)
        return NULL;
    result->type = FFI_TYPE_STRUCT;
    if (cache_add(ctx, id, result, 1) != 0) {
        free(result);
        return NULL;
    }

    if (ctf_member_iter(ctx->ctf, id, member_cb, &b) != 0 || b.error || !b.count) {
        free(b.elements);
        return NULL;
    }

    if (is_union) {
        ffi_type *largest = b.elements[0];
        for (size_t i = 1; i < b.count; ++i) {
            if (layout_struct(b.elements[i]) != 0) {
                free(b.elements);
                return NULL;
            }
            if (b.elements[i]->size > largest->size)
                largest = b.elements[i];
        }
        result->elements = malloc(2 * sizeof(*result->elements));
        if (!result->elements) {
            free(b.elements);
            return NULL;
        }
        result->elements[0] = largest;
        result->elements[1] = NULL;
        free(b.elements);
    } else {
        result->elements = realloc(b.elements, (b.count + 1) * sizeof(*result->elements));
        if (!result->elements)
            return NULL;
        result->elements[b.count] = NULL;
    }

    if (layout_struct(result) != 0) {
        free(result->elements);
        result->elements = NULL;
        return NULL;
    }

    ssize_t ctf_size = ctf_type_size(ctx->ctf, id);
    if (ctf_size < 0 || (size_t)ctf_size != result->size) {
        fprintf(stderr, "CTF/libffi size mismatch for %s type %lu: CTF=%zd libffi=%zu\n",
                is_union ? "union" : "struct", (unsigned long)id,
                ctf_size, result->size);
        return NULL;
    }
    return result;
}

static ffi_type *ctf_to_ffi_type(ctf_ffi_context_t *ctx, ctf_id_t id) {
    if (!ctx || id == CTF_ERR)
        return NULL;
    id = ctf_type_resolve(ctx->ctf, id);
    if (id == CTF_ERR)
        return NULL;
    ffi_type *cached = cache_find(ctx, id);
    if (cached)
        return cached;

    switch (ctf_type_kind(ctx->ctf, id)) {
    case CTF_K_INTEGER: return integer_type(ctx->ctf, id);
    case CTF_K_FLOAT: return float_type(ctx->ctf, id);
    case CTF_K_POINTER:
    case CTF_K_FUNCTION: return &ffi_type_pointer;
    case CTF_K_ENUM: return &ffi_type_sint32;
    case CTF_K_ARRAY: return &ffi_type_pointer;
    case CTF_K_STRUCT: return aggregate_type(ctx, id, 0);
    case CTF_K_UNION: return aggregate_type(ctx, id, 1);
    default: return NULL;
    }
}

int build_cif_from_ctf(ctf_ffi_context_t *ctx, const char *name,
                       ffi_cif *cif, ffi_type **rtype,
                       ffi_type ***args_out, size_t *nargs_out) {
    ctf_id_t fn = ctf_lookup_by_symbol_name(ctx->ctf, name);
    ctf_funcinfo_t info;
    if (fn == CTF_ERR || ctf_func_type_info(ctx->ctf, fn, &info) != 0)
        return -1;

    size_t n = info.ctc_argc;
    ctf_id_t *ids = n ? calloc(n, sizeof(*ids)) : NULL;
    ffi_type **args = n ? calloc(n, sizeof(*args)) : NULL;
    if (n && (!ids || !args)) {
        free(ids); free(args); return -1;
    }
    if (n && ctf_func_type_args(ctx->ctf, fn, n, ids) != 0) {
        free(ids); free(args); return -1;
    }

    *rtype = ctf_to_ffi_type(ctx, info.ctc_return);
    if (!*rtype) {
        free(ids); free(args); return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        args[i] = ctf_to_ffi_type(ctx, ids[i]);
        if (!args[i]) {
            free(ids); free(args); return -1;
        }
    }
    free(ids);
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, (unsigned)n, *rtype, args) != FFI_OK) {
        free(args); return -1;
    }
    *args_out = args;
    *nargs_out = n;
    return 0;
}

void *call_function_via_ctf(const char *path, const char *name,
                            void **values, size_t nargs) {
    void *handle = dlopen(path, RTLD_LAZY);
    ctf_ffi_context_t ctx;
    ffi_type *rtype = NULL;
    ffi_type **args = NULL;
    ffi_cif cif;
    size_t actual = 0;
    if (!handle || ctf_ffi_init(&ctx, path) != 0)
        return NULL;
    if (build_cif_from_ctf(&ctx, name, &cif, &rtype, &args, &actual) != 0 || actual != nargs)
        goto fail;

    void (*fn)(void) = (void (*)(void))dlsym(handle, name);
    if (!fn)
        goto fail;
    void *result = rtype->size ? calloc(1, rtype->size) : NULL;
    ffi_call(&cif, FFI_FN(fn), result, values);
    free(args);
    ctf_ffi_cleanup(&ctx);
    dlclose(handle);
    return result;
fail:
    free(args);
    ctf_ffi_cleanup(&ctx);
    dlclose(handle);
    return NULL;
}
