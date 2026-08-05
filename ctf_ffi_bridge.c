/*
 * CTF-FFI Bridge: Proof of Concept Implementation
 * 
 * This demonstrates using CTF debug metadata to provide type information
 * for libffi, enabling automatic function signature discovery.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <ffi.h>
#include <ctf-api.h>

#define MAX_ARGS 16
#define TYPE_CACHE_SIZE 256

/* Type cache entry */
typedef struct {
    ctf_id_t ctf_type_id;
    ffi_type *ffi_type;
    int is_dynamic;  /* 1 if ffi_type was dynamically allocated */
} type_cache_entry_t;

/* CTF-FFI context */
typedef struct {
    ctf_archive_t *arc;
    ctf_file_t *ctf;
    type_cache_entry_t type_cache[TYPE_CACHE_SIZE];
    int cache_count;
} ctf_ffi_context_t;

/* Forward declarations */
static ffi_type* ctf_to_ffi_type(ctf_ffi_context_t *ctx, ctf_id_t type_id);

/* Initialize CTF-FFI context */
int ctf_ffi_init(ctf_ffi_context_t *ctx, const char *lib_path) {
    memset(ctx, 0, sizeof(*ctx));
    
    /* Open CTF archive - libctf API requires error code pointer */
    int err;
    ctx->arc = ctf_arc_open(lib_path, &err);
    if (!ctx->arc) {
        fprintf(stderr, "Failed to open CTF archive for %s (error %d)\n", 
                lib_path, err);
        return -1;
    }
    
    /* Bind to CTF file */
    ctx->ctf = ctf_arc_open_by_name(ctx->arc, NULL, &err);
    if (!ctx->ctf) {
        fprintf(stderr, "Failed to bind CTF (error %d)\n", err);
        ctf_arc_close(ctx->arc);
        ctx->arc = NULL;
        return -1;
    }
    
    printf("CTF initialized successfully for %s\n", lib_path);
    return 0;
}

/* Cleanup CTF-FFI context */
void ctf_ffi_cleanup(ctf_ffi_context_t *ctx) {
    if (!ctx) return;
    
    /* Free dynamically allocated ffi_type structures */
    for (int i = 0; i < ctx->cache_count; i++) {
        if (ctx->type_cache[i].is_dynamic && ctx->type_cache[i].ffi_type) {
            ffi_type *ft = ctx->type_cache[i].ffi_type;
            
            /* Free struct elements if present */
            if (ft->type == FFI_TYPE_STRUCT && ft->elements) {
                free(ft->elements);
            }
            
            free(ft);
        }
    }
    
    /* Close CTF archive */
    if (ctx->ctf) {
        ctf_arc_close(ctx->arc);
    }
}

/* Look up type in cache */
static ffi_type* find_in_cache(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    for (int i = 0; i < ctx->cache_count; i++) {
        if (ctx->type_cache[i].ctf_type_id == type_id) {
            return ctx->type_cache[i].ffi_type;
        }
    }
    return NULL;
}

/* Add type to cache */
static void add_to_cache(ctf_ffi_context_t *ctx, ctf_id_t type_id, 
                         ffi_type *ffi_type, int is_dynamic) {
    if (ctx->cache_count >= TYPE_CACHE_SIZE) {
        fprintf(stderr, "Type cache full!\n");
        return;
    }
    
    ctx->type_cache[ctx->cache_count].ctf_type_id = type_id;
    ctx->type_cache[ctx->cache_count].ffi_type = ffi_type;
    ctx->type_cache[ctx->cache_count].is_dynamic = is_dynamic;
    ctx->cache_count++;
}

/* Convert CTF integer type to ffi_type */
static ffi_type* ctf_integer_to_ffi(ctf_file_t *ctf, ctf_id_t type_id) {
    unsigned long size = ctf_type_size(ctf, type_id);
    
    switch (size) {
        case 1: return &ffi_type_sint8;
        case 2: return &ffi_type_sint16;
        case 4: return &ffi_type_sint32;
        case 8: return &ffi_type_sint64;
        default:
            fprintf(stderr, "Unsupported integer size: %lu\n", size);
            return &ffi_type_void;
    }
}

/* Convert CTF float type to ffi_type */
static ffi_type* ctf_float_to_ffi(ctf_file_t *ctf, ctf_id_t type_id) {
    unsigned long size = ctf_type_size(ctf, type_id);
    
    switch (size) {
        case 4: return &ffi_type_float;
        case 8: return &ffi_type_double;
        default:
            fprintf(stderr, "Unsupported float size: %lu\n", size);
            return &ffi_type_void;
    }
}

/* Convert CTF struct/union to ffi_type */
static ffi_type* ctf_struct_to_ffi(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    ctf_file_t *ctf = ctx->ctf;
    unsigned long size = ctf_type_size(ctf, type_id);
    
    /* Allocate new ffi_type */
    ffi_type *ffi_struct = calloc(1, sizeof(ffi_type));
    if (!ffi_struct) {
        return &ffi_type_void;
    }
    
    ffi_struct->type = FFI_TYPE_STRUCT;
    ffi_struct->size = size;
    
    /* Get alignment from CTF */
    ffi_struct->alignment = ctf_type_align(ctf, type_id);
    
    /* For now, set elements to NULL (opaque struct) */
    /* TODO: Recursively process struct members */
    ffi_struct->elements = NULL;
    
    add_to_cache(ctx, type_id, ffi_struct, 1);
    
    return ffi_struct;
}

/* Main type conversion function */
static ffi_type* ctf_to_ffi_type(ctf_ffi_context_t *ctx, ctf_id_t type_id) {
    ctf_file_t *ctf = ctx->ctf;
    
    /* Check cache first */
    ffi_type *cached = find_in_cache(ctx, type_id);
    if (cached) {
        return cached;
    }
    
    /* Get type kind */
    int kind = ctf_type_kind(ctf, type_id);
    
    ffi_type *result;
    switch (kind) {
        case CTF_K_INTEGER:
            result = ctf_integer_to_ffi(ctf, type_id);
            break;
            
        case CTF_K_FLOAT:
            result = ctf_float_to_ffi(ctf, type_id);
            break;
            
        case CTF_K_POINTER:
            result = &ffi_type_pointer;
            break;
            
        case CTF_K_STRUCT:
        case CTF_K_UNION:
            result = ctf_struct_to_ffi(ctx, type_id);
            break;
            
        case CTF_K_ENUM:
            /* Treat enums as integers */
            result = &ffi_type_sint32;
            break;
            
        case CTF_K_ARRAY:
            /* Arrays decay to pointers in FFI */
            result = &ffi_type_pointer;
            break;
            
        case CTF_K_FUNCTION:
            /* Function types handled specially */
            result = &ffi_type_pointer;
            break;
            
        default:
            fprintf(stderr, "Unknown CTF type kind: %d\n", kind);
            result = &ffi_type_void;
            break;
    }
    
    /* Cache non-primitive types */
    if (kind != CTF_K_INTEGER && kind != CTF_K_FLOAT && 
        kind != CTF_K_POINTER) {
        add_to_cache(ctx, type_id, result, (kind == CTF_K_STRUCT || kind == CTF_K_UNION));
    }
    
    return result;
}

/* Build ffi_cif from CTF function type */
int build_cif_from_ctf(ctf_ffi_context_t *ctx, const char *func_name,
                       ffi_cif *cif, ffi_type **rtype, ffi_type **args) {
    ctf_file_t *ctf = ctx->ctf;
    
    /* Look up function type by name */
    ctf_id_t func_type_id = ctf_lookup_by_name(ctf, func_name);
    if (func_type_id == CTF_ERR) {
        fprintf(stderr, "Function '%s' not found in CTF\n", func_name);
        return -1;
    }
    
    /* Get function info - libctf API uses different signature */
    ctf_funcinfo_t finfo;
    int err = ctf_func_info(ctf, func_type_id, &finfo);
    if (err != 0) {
        fprintf(stderr, "Failed to get function info for '%s'\n", func_name);
        return -1;
    }
    
    /* Get argument types */
    ctf_id_t arg_types[MAX_ARGS];
    int nargs = finfo.ctc_argc;
    if (nargs > MAX_ARGS) {
        fprintf(stderr, "Too many arguments (%d) for function '%s'\n", nargs, func_name);
        nargs = MAX_ARGS;
    }
    
    err = ctf_func_args(ctf, func_type_id, nargs, arg_types);
    if (err != 0) {
        fprintf(stderr, "Failed to get function arguments for '%s'\n", func_name);
        return -1;
    }
    
    printf("Function '%s': %d arguments\n", func_name, nargs);
    
    /* Convert return type */
    *rtype = ctf_to_ffi_type(ctx, finfo.ctc_return);
    
    /* Convert argument types */
    for (int i = 0; i < nargs; i++) {
        args[i] = ctf_to_ffi_type(ctx, arg_types[i]);
        
        /* Debug: print type info */
        #ifdef DEBUG
        int kind = ctf_type_kind(ctf, arg_types[i]);
        printf("  Arg %d: kind=%d, size=%lu\n", i, kind, 
               ctf_type_size(ctf, arg_types[i]));
        #endif
    }
    
    /* Prepare call interface */
    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, *rtype, args);
    if (status != FFI_OK) {
        fprintf(stderr, "Failed to prepare CIF\n");
        return -1;
    }
    
    return nargs;
}

/* Call function using CTF-derived types */
void* call_function_via_ctf(const char *lib_path, const char *func_name,
                            void **arg_values, int nargs) {
    void *result = NULL;
    
    /* Open shared library */
    void *handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return NULL;
    }
    
    /* Initialize CTF */
    ctf_ffi_context_t ctx;
    if (ctf_ffi_init(&ctx, lib_path) != 0) {
        dlclose(handle);
        return NULL;
    }
    
    /* Prepare FFI types */
    ffi_type *rtype;
    ffi_type *args[MAX_ARGS];
    ffi_cif cif;
    
    int actual_nargs = build_cif_from_ctf(&ctx, func_name, &cif, &rtype, args);
    if (actual_nargs < 0) {
        ctf_ffi_cleanup(&ctx);
        dlclose(handle);
        return NULL;
    }
    
    /* Get function pointer */
    void (*func)() = dlsym(handle, func_name);
    if (!func) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        ctf_ffi_cleanup(&ctx);
        dlclose(handle);
        return NULL;
    }
    
    /* Allocate return value buffer */
    void *retval = NULL;
    if (rtype->size > 0) {
        retval = malloc(rtype->size);
        if (!retval) {
            fprintf(stderr, "Failed to allocate return value buffer\n");
            ctf_ffi_cleanup(&ctx);
            dlclose(handle);
            return NULL;
        }
    }
    
    /* Call function through libffi */
    ffi_call(&cif, FFI_FN(func), retval, arg_values);
    
    result = retval;
    
    /* Cleanup */
    ctf_ffi_cleanup(&ctx);
    dlclose(handle);
    
    return result;
}

/* Utility: List all functions available in CTF */
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
    
    printf("Functions in %s:\n", lib_path);
    
    /* Iterate through labels to find functions */
    ctf_next_t *i = NULL;
    ctf_id_t id;
    while ((id = ctf_type_next(ctf, &i, NULL, 0)) != 0) {
        int kind = ctf_type_kind(ctf, id);
        if (kind == CTF_K_FUNCTION) {
            char name_buf[256];
            char *name = ctf_type_name(ctf, id, name_buf, sizeof(name_buf));
            if (name) {
                printf("  - %s\n", name);
            }
        }
    }
    
    ctf_arc_close(arc);
    return 0;
}

#ifdef STANDALONE_TEST
/* Standalone test program */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <library.so> <function> [args...]\n", argv[0]);
        return 1;
    }
    
    const char *lib_path = argv[1];
    const char *func_name = argv[2];
    
    /* Test: List functions */
    printf("=== Listing CTF Functions ===\n");
    list_ctf_functions(lib_path);
    printf("\n");
    
    /* Test: Call function */
    printf("=== Calling %s ===\n", func_name);
    
    /* Example: call a simple function with no arguments */
    void *arg_values[] = { NULL };
    void *result = call_function_via_ctf(lib_path, func_name, arg_values, 0);
    
    if (result) {
        printf("Function returned: %p\n", result);
        free(result);
    }
    
    return 0;
}
#endif
