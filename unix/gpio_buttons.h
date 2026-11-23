#ifndef _GPIO_BUTTONS_H_
#define _GPIO_BUTTONS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GKeyFile GKeyFile;

typedef struct {
    int enabled;
    int up;
    int down;
    int left;
    int right;
    int a;
    int b;
    int x;
    int y;
    int l;
    int r;
    int start;
    int select;
    int quit;
} gpio_config_t;

extern gpio_config_t g_gpio_cfg;

void gpio_buttons_init(GKeyFile *keyfile);
uint32_t gpio_buttons_read(int *quit_requested);
void gpio_buttons_close(void);

#ifdef __cplusplus
}
#endif

#endif
