#ifndef _WAPI_PROCESS_PRIVATE_H_
#define _WAPI_PROCESS_PRIVATE_H_

#include <config.h>
#include <glib.h>

struct _WapiHandle_process
{
	pid_t id;
};

struct _WapiHandlePrivate_process
{
	int dummy;
};

#endif /* _WAPI_PROCESS_PRIVATE_H_ */
