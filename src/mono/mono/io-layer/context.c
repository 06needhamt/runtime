#include <config.h>
#include <glib.h>
#include <pthread.h>

#include "mono/io-layer/wapi.h"

gboolean GetThreadContext(gpointer handle G_GNUC_UNUSED, WapiContext *context G_GNUC_UNUSED)
{
	return(FALSE);
}



