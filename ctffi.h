#ifndef CTFFI_H
#define CTFFI_H

#include <ctf-api.h>
#include <stddef.h>
#include <ffi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CTFFI_TYPE_CACHE_SIZE 256

typedef struct {
    ctf_id_t id;
    ffi_type *type;
    int dynamic;
} ctffi_type_cache_entry_t;

typedef struct ctf_ffi_context {
    ctf_archive_t *archive;
    ctf_file_t *ctf;
    ctffi_type_cache_entry_t cache[CTFFI_TYPE_CACHE_SIZE];
    size_t cache_count;
} ctf_ffi_context_t;

int ctf_ffi_init(ctf_ffi_context_t *ctx, const char *path);
void ctf_ffi_cleanup(ctf_ffi_context_t *ctx);

int build_cif_from_ctf(ctf_ffi_context_t *ctx, const char *name,
                       ffi_cif *cif, ffi_type **rtype,
                       ffi_type ***args_out, size_t *nargs_out);

void *call_function_via_ctf(const char *path, const char *name,
                            void **values, size_t nargs);

#ifdef __cplusplus
}
#endif

#endif
