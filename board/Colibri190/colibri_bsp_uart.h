#ifndef _TCL_COLIBRI_UART_H
#define _TCL_COLIBRI_UART_H


extern void EvbUart2Config(void);
extern void EvbUart2ReadByte(char* c);
extern void EvbUart2WriteByte(char c);
extern void EvbUart2WriteStr(const char* str);

#endif /* _TCL_COLIBRI_UART_H */
