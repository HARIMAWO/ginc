#ifndef CPP_H
#define CPP_H

extern char* ginc_preprocess_file(char const* filename, char const* const* include_dirs, int include_dir_count);
extern char* ginc_preprocess_string(char const* src, char const* filename, char const* const* include_dirs, int include_dir_count);

#endif
