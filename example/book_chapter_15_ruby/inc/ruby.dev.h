#include "example.h"
#include "trochili.h"

#if (EVB_EXAMPLE == CH15_RUBY_EXAMPLE)
#include "ruby.h"

#define IO_DATA_RXBUF_SIZE      (256)

typedef struct
{
    struct
    {
        UINT8*  RxBufRdPtr;
        UINT8*  RxBufWrPtr;
        UINT32  RxBufCount;

        UINT8*  TxBufPtr;
        UINT32  TxBufCount;
    } DATA;
} CoIODevCtrl;

extern CoIODevCtrl UartDev;

#endif

