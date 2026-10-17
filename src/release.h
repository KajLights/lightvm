#ifndef LIGHTVM_RELEASE_H
#define LIGHTVM_RELEASE_H
#include <stdbool.h>
#include "lightvm_release.h"

/* No allocation. Build metadata is ignored; prereleases follow SemVer ordering. */
bool lightvm_release_valid(const char *value, bool shorthand);
const char *lightvm_release_error(const char *current, const char *required);
#endif
