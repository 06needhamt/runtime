#include <config.h>
#include <glib.h>
#include <pthread.h>
#include <iconv.h>
#include <errno.h>

#include "mono/io-layer/wapi.h"
#include "unicode.h"

/* This is a nasty kludge */
static guint32 unicode_len(const gunichar2 *str)
{
	guint32 len=0;
	
	do {
		if(str[len]=='\0' && str[len+1]=='\0') {
			return(len);
		}

		len+=2;
	} while(1);
}

gchar *_wapi_unicode_to_utf8(const gunichar2 *uni)
{
	GError *error = NULL;
	gchar *res;
	int len;

	len = unicode_len(uni);
	
	res = g_utf16_to_utf8 (uni, len, NULL, NULL, &error);

	g_assert (!error);

	return res;
}
