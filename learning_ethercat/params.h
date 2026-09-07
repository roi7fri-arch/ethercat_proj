#ifndef ParamsH
#define ParamsH

#define MAX_PARAMS    2000

typedef union
{
  struct
  {
    float a[MAX_PARAMS];
  } Info;
  byte Buf[MAX_PARAMS*4];
} ParamsType;

class TPrms
{
private:
ParamsType PrmData;
bool GetVariable(char *str, float *n);

public:
TPrms(void);
~TPrms(void);
bool Open(char *fname);

unsigned int m_uParamsNo;
unsigned int m_ubytesToSend;
char *m_pProgBuf;
};

#endif
