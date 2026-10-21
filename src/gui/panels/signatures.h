#ifndef CDIE_SIGNATURES_H
#define CDIE_SIGNATURES_H

#include <xxwidgets/xxwidgets.h>

#ifdef __cplusplus
extern "C" {
#endif

	/* Modal signatures window. Call from the main loop, not from a callback. */
	void signatures_show(xxwidgets_app* app, xxwidgets_widget* owner);

	/* Call first in your on_event. Returns 1 if the event was consumed. */
	int signatures_handle_event(const xxwidgets_event* event);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_SIGNATURES_H */