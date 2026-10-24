#pragma once
#include <stdbool.h>
bool device_version_valid(const char *,bool shorthand);
const char *device_version_error(const char *current,const char *required);
bool device_version_at_least(const char *,const char *);
