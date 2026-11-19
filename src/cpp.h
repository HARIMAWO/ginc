/*
 * Copyright 2026 HARIMAWO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef CPP_H
#define CPP_H

extern char* ginc_preprocess_file(char const* filename, char const* const* include_dirs, int include_dir_count);
extern char* ginc_preprocess_string(char const* src, char const* filename, char const* const* include_dirs, int include_dir_count);

#endif
