/* No terminals: newlib has no window-size request. The console's VFS has
   no ioctl, so the query fails with ENOSYS (esp_vfs_ioctl). */
#ifndef SP_ESP_SYS_IOCTL_H
#define SP_ESP_SYS_IOCTL_H
#include_next <sys/ioctl.h>
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x5413
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#endif
#endif
