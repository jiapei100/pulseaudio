/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "module.h"
#include "sink.h"
#include "scache.h"
#include "modargs.h"
#include "xmalloc.h"
#include "namereg.h"
#include "log.h"
#include "x11wrap.h"
#include "util.h"

#include "module-x11-publish-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering")
PA_MODULE_DESCRIPTION("X11 Credential Publisher")
PA_MODULE_VERSION(PACKAGE_VERSION)
PA_MODULE_USAGE("display=<X11 display>")

static const char* const valid_modargs[] = {
    "display",
    "sink",
    "source",
    NULL
};

struct userdata {
    struct pa_core *core;
    struct pa_x11_wrapper *x11_wrapper;
    Display *display;
    char *id;
};

static void set_x11_prop(Display *d, const char *name, const char *data) {
    Atom a = XInternAtom(d, name, False);
    XChangeProperty(d, RootWindow(d, 0), a, XA_STRING, 8, PropModeReplace, (unsigned char*) data, strlen(data)+1);
}

static void del_x11_prop(Display *d, const char *name) {
    Atom a = XInternAtom(d, name, False);
    XDeleteProperty(d, RootWindow(d, 0), a);
}

static char* get_x11_prop(Display *d, const char *name, char *p, size_t l) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long nbytes_after;
    unsigned char *prop;
    
    Atom a = XInternAtom(d, name, False);
    if (XGetWindowProperty(d, RootWindow(d, 0), a, 0, (l+2)/4, False, XA_STRING, &actual_type, &actual_format, &nitems, &nbytes_after, &prop) != Success)
        return NULL;

    memcpy(p, prop, nitems);
    p[nitems] = 0;

    XFree(prop);
    return p;
}

int pa__init(struct pa_core *c, struct pa_module*m) {
    struct userdata *u;
    struct pa_modargs *ma = NULL;
    char hn[256], un[128];
     const char *t;

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log(__FILE__": failed to parse module arguments\n");
        goto fail;
    }

    m->userdata = u = pa_xmalloc(sizeof(struct userdata));
    u->core = c;
    u->id = NULL;

    if (!(u->x11_wrapper = pa_x11_wrapper_get(c, pa_modargs_get_value(ma, "display", NULL)))) 
        goto fail;

    u->display = pa_x11_wrapper_get_display(u->x11_wrapper);

    pa_get_host_name(hn, sizeof(hn));
    pa_get_user_name(un, sizeof(un));
    
    u->id = pa_sprintf_malloc("%s@%s/%u", un, hn, (unsigned) getpid());

    set_x11_prop(u->display, "POLYP_SERVER", hn);
    set_x11_prop(u->display, "POLYP_ID", u->id);

    if ((t = pa_modargs_get_value(ma, "source", NULL)))
        set_x11_prop(u->display, "POLYP_SOURCE", t);

    if ((t = pa_modargs_get_value(ma, "sink", NULL)))
        set_x11_prop(u->display, "POLYP_SINK", t);
    
    pa_modargs_free(ma);
    return 0;
    
fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(c, m);
    return -1;
}

void pa__done(struct pa_core *c, struct pa_module*m) {
    struct userdata*u;
    assert(c && m);

    if (!(u = m->userdata))
        return;
    
    if (u->x11_wrapper) {
        char t[256];

        /* Yes, here is a race condition */
        if (!get_x11_prop(u->display, "POLYP_ID", t, sizeof(t)) || strcmp(t, u->id))
            pa_log("WARNING: Polypaudio information vanished from X11!\n");
        else {
            del_x11_prop(u->display, "POLYP_ID");
            del_x11_prop(u->display, "POLYP_SERVER");
            del_x11_prop(u->display, "POLYP_SINK");
            del_x11_prop(u->display, "POLYP_SOURCE");
            XSync(u->display, False);
        }
    }
    
    if (u->x11_wrapper)
        pa_x11_wrapper_unref(u->x11_wrapper);

    pa_xfree(u->id);
    pa_xfree(u);
}

