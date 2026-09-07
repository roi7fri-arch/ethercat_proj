// Copyright 1997-98 (c) Iuri Apollonio
// given freely to www.codeguru.com

// Ini.cpp: implementation of the CIni class.
//
//////////////////////////////////////////////////////////////////////


#include <stdio.h>
#include "ini.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

FILE *fp;

int ini_init(char *filename)
{
	if ((fp = fopen(filename, "rt")) == NULL)
		return 0;
	return 1;
}

int ini_close(void)
{
	fclose(fp);
}

int ini_find_item_string(const char *item, char *s)
{
	int res = 1;
	char txt[80];
	char *p;

	fseek(fp, 0, SEEK_SET);
	do
	{
		fgets(txt, 80, fp);
		res = strncmp(item, txt, 3);
		if (res == 0)
			break;
	} while (!feof(fp));

	if (res != 0)
		return 0;

	// scan the string for = sign and string value
	p = txt;
	while ((*p != '=') && (*p != '\0'))
	{
		p++;
	}

	if (*p != '=')
		return 0;

	while (!isalnum(*p) && (*p != '\0'))
	{
		p++;
	}

	if (!isalnum(*p))
		return 0;

	strcpy(s, p);
	return 1;
}

int ini_get_float(const char * item, float *fval)
{
	char s[80];
	if (ini_find_item_string(item, s) == 0)
		return 0;

	*fval = (float) atof(s);
	return 1;
}

int ini_get_double(const char * item, double *dbval)
{
	char s[80];
	if (ini_find_item_string(item, s) == 0)
		return 0;

	*dbval = (double) atof(s);
	return 1;
}

int ini_get_int(const char *item, int *ival)
{
	char s[80];

	if (ini_find_item_string(item, s) == 0)
		return 0;

	*ival = atoi(s);
	return 1;
}

int ini_get_long(const char * item, long *lval)
{
	char s[80];
	if (ini_find_item_string(item, s) == 0)
		return 0;

	*lval = (long) atol(s);
	return 1;
}

int ini_get_short(const char * item, short *shval)
{
	char s[80];
	if (ini_find_item_string(item, s) == 0)
		return 0;

	*shval = (short) atoi(s);
	return 1;
}

int ini_get_string(const char * item, char *cval)
{
	char s[80];

	if (ini_find_item_string(item, s) == 0)
		return 0;

	strcpy(cval, s);
	return 1;
}

int ini_get_hex(const char * item, int *hval)
{
	char s[80];
	if (ini_find_item_string(item, s) == 0)
		return 0;
	if (sscanf(s, "%x", hval) == 0)
		return 0;
	return 1;
}




