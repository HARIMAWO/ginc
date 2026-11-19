#ifndef GINC_H
#define GINC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GincEngine GincEngine;

extern GincEngine* ginc_create(void);
extern void ginc_destroy(GincEngine* eng);
extern void ginc_add_include_dir(GincEngine* eng, char const* dir);
extern void ginc_add_lib_dir(GincEngine* eng, char const* dir);
extern void ginc_set_verbose(GincEngine* eng, int verbose);
extern int ginc_eval_file(GincEngine* eng, char const* filename, int argc, char** argv);
extern int ginc_eval_string(GincEngine* eng, char const* src, char const* filename, int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif
