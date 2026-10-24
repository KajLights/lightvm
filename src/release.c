#include "release.h"
#include "device/version.h"
#include <string.h>
bool lightvm_release_valid(const char *v,bool shorthand) { return device_version_valid(v,shorthand); }
bool lightvm_release_at_least(const char *v,const char *floor) { return device_version_at_least(v,floor); }
const char *lightvm_release_error(const char *current,const char *required) {
    const char *error=device_version_error(current,required);
    if (!error) return NULL;
    if (!strcmp(error,"invalid device application release")) return "invalid LightVM application release";
    if (!strcmp(error,"invalid minimum required device release")) return "invalid minimum required LightVM release";
    if (!strcmp(error,"program requires a different device major release")) return "program requires a different LightVM major release";
    return "program requires a newer LightVM release";
}
