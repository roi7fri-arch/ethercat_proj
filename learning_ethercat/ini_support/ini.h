// Copyright 1997-98 (c) Iuri Apollonio
// given freely to www.codeguru.com


// Ini.h: interface for the CIni class.
//
//////////////////////////////////////////////////////////////////////



#ifndef _INI_H_
#define _INI_H_

	int ini_init(char *filename);
	int ini_close(void);
	int ini_find_item(const char *item, char *s);
	int ini_get_int(const char *item, int *ival);
	int ini_get_float(const char * item, float *fval);
	int ini_get_double(const char * item, double *dbval);
	int ini_get_long(const char * item, long *lval);
	int ini_get_short(const char * item, short *shval);
	int ini_get_string(const char * item, char *cval);
	int ini_get_hex(const char * item, int *hval);

#endif

