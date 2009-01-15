/***
  This file is part of PulseAudio.

  Copyright 2006-2008 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>

#include <pulse/timeval.h>
#include <pulse/util.h>

#include <pulsecore/core-util.h>
#include <pulsecore/module.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-error.h>

#include "module-default-device-restore-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Automatically restore the default sink and source");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

#define DEFAULT_SAVE_INTERVAL 5

struct userdata {
    pa_core *core;
    pa_subscription *subscription;
    pa_time_event *time_event;
    char *sink_filename, *source_filename;
    pa_bool_t modified;
};

static void load(struct userdata *u) {
    FILE *f;

    /* We never overwrite manually configured settings */

    if (u->core->default_sink_name)
        pa_log_info("Manually configured default sink, not overwriting.");
    else if ((f = fopen(u->sink_filename, "r"))) {
        char ln[256] = "";

        fgets(ln, sizeof(ln)-1, f);
        pa_strip_nl(ln);
        fclose(f);

        if (!ln[0])
            pa_log_info("No previous default sink setting, ignoring.");
        else if (pa_namereg_get(u->core, ln, PA_NAMEREG_SINK)) {
            pa_namereg_set_default(u->core, ln, PA_NAMEREG_SINK);
            pa_log_info("Restored default sink '%s'.", ln);
        } else
            pa_log_info("Saved default sink '%s' not existant, not restoring default sink setting.", ln);

    } else if (errno != ENOENT)
        pa_log("Failed to load default sink: %s", pa_cstrerror(errno));

    if (u->core->default_source_name)
        pa_log_info("Manually configured default source, not overwriting.");
    else if ((f = fopen(u->source_filename, "r"))) {
        char ln[256] = "";

        fgets(ln, sizeof(ln)-1, f);
        pa_strip_nl(ln);
        fclose(f);

        if (!ln[0])
            pa_log_info("No previous default source setting, ignoring.");
        else if (pa_namereg_get(u->core, ln, PA_NAMEREG_SOURCE)) {
            pa_namereg_set_default(u->core, ln, PA_NAMEREG_SOURCE);
            pa_log_info("Restored default source '%s'.", ln);
        } else
            pa_log_info("Saved default source '%s' not existant, not restoring default source setting.", ln);

    } else if (errno != ENOENT)
            pa_log("Failed to load default sink: %s", pa_cstrerror(errno));
}

static void save(struct userdata *u) {
    FILE *f;

    if (!u->modified)
        return;

    if (u->sink_filename) {
        if ((f = fopen(u->sink_filename, "w"))) {
            const char *n = pa_namereg_get_default_sink_name(u->core);
            fprintf(f, "%s\n", pa_strempty(n));
            fclose(f);
        } else
            pa_log("Failed to save default sink: %s", pa_cstrerror(errno));
    }

    if (u->source_filename) {
        if ((f = fopen(u->source_filename, "w"))) {
            const char *n = pa_namereg_get_default_source_name(u->core);
            fprintf(f, "%s\n", pa_strempty(n));
            fclose(f);
        } else
            pa_log("Failed to save default source: %s", pa_cstrerror(errno));
    }

    u->modified = FALSE;
}

static void time_cb(pa_mainloop_api *a, pa_time_event *e, const struct timeval *tv, void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);
    save(u);

    if (u->time_event) {
        u->core->mainloop->time_free(u->time_event);
        u->time_event = NULL;
    }
}

static void subscribe_cb(pa_core *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);

    u->modified = TRUE;

    if (!u->time_event) {
        struct timeval tv;
        pa_gettimeofday(&tv);
        pa_timeval_add(&tv, DEFAULT_SAVE_INTERVAL*PA_USEC_PER_SEC);
        u->time_event = u->core->mainloop->time_new(u->core->mainloop, &tv, time_cb, u);
    }
}

int pa__init(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;

    if (!(u->sink_filename = pa_state_path("default-sink", TRUE)))
        goto fail;

    if (!(u->source_filename = pa_state_path("default-source", TRUE)))
        goto fail;

    load(u);

    u->subscription = pa_subscription_new(u->core, PA_SUBSCRIPTION_MASK_SERVER, subscribe_cb, u);

    return 0;

fail:
    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    save(u);

    if (u->subscription)
        pa_subscription_free(u->subscription);

    if (u->time_event)
        m->core->mainloop->time_free(u->time_event);

    pa_xfree(u->sink_filename);
    pa_xfree(u->source_filename);
    pa_xfree(u);
}
