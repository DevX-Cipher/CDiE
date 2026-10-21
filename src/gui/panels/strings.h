#pragma once
#include <xxwidgets/xxwidgets.h>

/* Shows a modal window listing the printable strings (ASCII and UTF-16LE)
 * found in `path`, with a live filter box. Blocks until the window closes. */
void strings_show(xxwidgets_app* app, xxwidgets_widget* owner, const char* path);