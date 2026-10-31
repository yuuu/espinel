/* No terminals: the window-size query answers ENOTTY (sp_esp.c). */
#ifndef SP_ESP_SYS_IOCTL_H
#define SP_ESP_SYS_IOCTL_H
#include_next <sys/ioctl.h>
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#endif
#endif
