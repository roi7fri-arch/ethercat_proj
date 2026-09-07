//---------------------------------------------------------------------------
#include "stdafx.h"
#include <stdio.h>
#include "params.h"
#include "Global.h"

//---------------------------------------------------------------------------
TPrms::TPrms(void)
{
  m_pProgBuf = NULL;
}
//---------------------------------------------------------------------------
TPrms::~TPrms(void)
{
  if (m_pProgBuf != NULL)
    delete m_pProgBuf;
}
//---------------------------------------------------------------------------
bool TPrms::GetVariable(char *str, float *n)
{
  float f;
  char *p = str;

  if (*p == '\n')   // empty line
    return false;
  if (!isalpha(*p)) // not starting with character
    return false;
  if (strchr(str, '=') == NULL)  // '=' not found
    return false;

  while (*p != '=')    // eliminate leading spaces
    p++;

  do
  {
    if (*p == '-')
      break;
    if (*p == '+')
      break;
    if (isdigit(*p))
      break;
      ++p;
  } while (*p != NULL);

  if (*p == '\n')   // end of string
    return false;

  f = (float)atof(p);
  *n = f;
  return true;
}
//---------------------------------------------------------------------------
bool TPrms::Open(char *fname)
{
  m_uParamsNo = 0;
  FILE *fp;
  char txt[80];
  float tmpf;

  memset(PrmData.Buf, 0xff, sizeof(PrmData.Buf));
  m_uParamsNo = 0;
  if ((fp = fopen(fname, "rt")) == NULL)
    return false;

  // extracting data from file
  do
  {
    fgets(txt, 80, fp);
    bool res = GetVariable(txt, &tmpf);
    if (res)
    {
      PrmData.Info.a[m_uParamsNo] = tmpf;
      if (++m_uParamsNo >= MAX_PARAMS)
        m_uParamsNo = MAX_PARAMS;
    }
  } while (!feof(fp));
  fclose(fp);

  m_ubytesToSend = m_uParamsNo * 4;   // each float 4 bytes
  int TotalSendLoops = m_ubytesToSend / BLOCK_SIZE +1;
  
  if (m_pProgBuf != NULL)
  {
    delete m_pProgBuf;
  }

  if ((m_pProgBuf = new char [TotalSendLoops * BLOCK_SIZE +1]) == NULL)
  {
    fclose(fp);
    return false;
  }

  memset(m_pProgBuf, 0xff, TotalSendLoops * BLOCK_SIZE +1);
  memmove(m_pProgBuf, PrmData.Buf, m_uParamsNo*4);
  return true;
}
//---------------------------------------------------------------------------




