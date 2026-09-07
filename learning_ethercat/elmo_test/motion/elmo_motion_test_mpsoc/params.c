#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "params.h"

#define MAX_PARAMS 20000



int GetVariable(char *str, short *n)
{
  short f;
  char *p = str;

  if (*p == '\n')   // empty line
    return 0;

  do
  {
    if (*p == '-')
      break;
    if (*p == '+')
      break;
    if (isdigit(*p))
      break;
      ++p;
  } while (*p != 0);

  if (*p == '\n')   // end of string
    return 0;
  f = (short)atoi(p);
  *n = f;
  return 1;
}


int open_out_file(char *fname, short* buf, int size)
{
  int m_uParamsNo = 0;
  FILE *fp;
  char txt[80];
  short tmpf;

  m_uParamsNo = 0;
  if ((fp = fopen(fname, "rt")) == NULL)
    return 0;
  // extracting data from file
  do
  {
    fgets(txt, 80, fp);
    int res = GetVariable(txt, &tmpf);
    if (res)
    {
      *buf++ = tmpf;
      if (++m_uParamsNo >= MAX_PARAMS)
        m_uParamsNo = MAX_PARAMS;
    }
  } while (!feof(fp));
  fclose(fp);



  return 1;
}

