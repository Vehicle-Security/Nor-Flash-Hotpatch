#include "patch_control.h"

#include "compare/src/compare.h"

#include "core/patch/clearbit_patch.h"
#include "core/target/targets.h"

const char *patch_scheme_name(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return "rapid";
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return "hera";
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return "autopatch";
    }
    return "legacy";
}

int patch_call(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_slot();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return fun1();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_patch_slot();
    }
    return patch_slot();
}

bool patch_apply(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_install();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return hera_patch_install();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_patch_install();
    }
    return clearbit_patch_apply();
}

void patch_unapply(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        rapid_patch_unapply();
    } else if (scheme == PATCH_SCHEME_HERA) {
        hera_patch_unapply();
    } else if (scheme == PATCH_SCHEME_AUTOPATCH) {
        autopatch_patch_unapply();
    } else {
        clearbit_patch_unapply();
    }
}

bool patch_is_active(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_is_active();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return hera_patch_is_active();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_patch_is_active();
    }
    return clearbit_patch_is_active();
}

bool patch_demo_can_run(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return true;
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return true;
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_is_ready();
    }
    return clearbit_patch_demo_can_run();
}

bool patch_supports_online_toggle(patch_scheme_t scheme) {
    (void)scheme;
    return true;
}

void print_patch_status(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        rapid_patch_print_status();
        return;
    }

    if (scheme == PATCH_SCHEME_HERA) {
        hera_patch_print_status();
        return;
    }

    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        autopatch_print_status();
        return;
    }

    clearbit_patch_print_status();
}

void print_all_patch_status(void) {
    print_patch_status(PATCH_SCHEME_RAPID);
    print_patch_status(PATCH_SCHEME_HERA);
    print_patch_status(PATCH_SCHEME_LEGACY);
    print_patch_status(PATCH_SCHEME_AUTOPATCH);
}
