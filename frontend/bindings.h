/* Key bindings (config.h) against the keys platform_poll_keys() saw. */
#ifndef BINDINGS_H
#define BINDINGS_H

#include <stddef.h>

#include "config.h"

/* The binding's key is down, and so is its 'with' key if it has one. */
int binding_held(struct Binding b);
/* ... and it wasn't at the poll before: one of its keys just went down. */
int binding_pressed(struct Binding b);
/* "ctrl", "shift+S", or "(none)". Returns text. */
const char *binding_name(struct Binding b, char *text, size_t size);

#endif
