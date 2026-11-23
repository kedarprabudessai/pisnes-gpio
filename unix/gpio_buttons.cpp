#include "gpio_buttons.h"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

// SNES button masks (matching snes9x.h)
#ifndef SNES_TR_MASK
#define SNES_TR_MASK        (1 << 4)
#define SNES_TL_MASK        (1 << 5)
#define SNES_X_MASK         (1 << 6)
#define SNES_A_MASK         (1 << 7)
#define SNES_RIGHT_MASK     (1 << 8)
#define SNES_LEFT_MASK      (1 << 9)
#define SNES_DOWN_MASK      (1 << 10)
#define SNES_UP_MASK        (1 << 11)
#define SNES_START_MASK     (1 << 12)
#define SNES_SELECT_MASK    (1 << 13)
#define SNES_Y_MASK         (1 << 14)
#define SNES_B_MASK         (1 << 15)
#endif

gpio_config_t g_gpio_cfg;
static volatile uint32_t *gpio_mmap_ptr = NULL;
static int gpio_mem_fd = -1;

static int get_gpio_conf(GKeyFile *keyfile, const char *option, int defval)
{
    if (!keyfile) return defval;
    GError *error = NULL;
    int val = g_key_file_get_integer(keyfile, "GPIO", option, &error);
    if (!error)
        return val;
    if (error)
        g_error_free(error);
    return defval;
}

// Configure a GPIO pin as input in GPFSEL register (bits 000)
static void set_gpio_input(int pin)
{
    if (gpio_mmap_ptr == NULL || pin < 0 || pin >= 32) return;
    int reg_idx = pin / 10;
    int shift = (pin % 10) * 3;
    // Set 3 bits to 000 (input mode)
    gpio_mmap_ptr[reg_idx] &= ~(7u << shift);
}

void gpio_buttons_init(GKeyFile *keyfile)
{
    memset(&g_gpio_cfg, 0, sizeof(g_gpio_cfg));

    // Default pin mapping (safe pins on Raspberry Pi 40-pin header)
    g_gpio_cfg.enabled = get_gpio_conf(keyfile, "Enabled", 0);
    g_gpio_cfg.up     = get_gpio_conf(keyfile, "UP", 6);
    g_gpio_cfg.down   = get_gpio_conf(keyfile, "DOWN", 13);
    g_gpio_cfg.left   = get_gpio_conf(keyfile, "LEFT", 19);
    g_gpio_cfg.right  = get_gpio_conf(keyfile, "RIGHT", 26);
    g_gpio_cfg.a      = get_gpio_conf(keyfile, "A", 4);
    g_gpio_cfg.b      = get_gpio_conf(keyfile, "B", 17);
    g_gpio_cfg.x      = get_gpio_conf(keyfile, "X", 27);
    g_gpio_cfg.y      = get_gpio_conf(keyfile, "Y", 22);
    g_gpio_cfg.l      = get_gpio_conf(keyfile, "L", 10);
    g_gpio_cfg.r      = get_gpio_conf(keyfile, "R", 9);
    g_gpio_cfg.start  = get_gpio_conf(keyfile, "START", 11);
    g_gpio_cfg.select = get_gpio_conf(keyfile, "SELECT", 5);
    g_gpio_cfg.quit   = get_gpio_conf(keyfile, "QUIT", 21);

    if (!g_gpio_cfg.enabled) {
        return;
    }

    gpio_mem_fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
    if (gpio_mem_fd < 0) {
        // Fallback to /dev/mem if /dev/gpiomem is unavailable (requires root)
        gpio_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (gpio_mem_fd < 0) {
            printf("[GPIO] Notice: /dev/gpiomem could not be opened. Direct GPIO input disabled.\n");
            g_gpio_cfg.enabled = 0;
            return;
        }
        // BCM2835 GPIO base physical address is 0x20200000
        gpio_mmap_ptr = (volatile uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_mem_fd, 0x20200000);
    } else {
        // /dev/gpiomem maps GPIO register base directly at offset 0
        gpio_mmap_ptr = (volatile uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_mem_fd, 0);
    }

    if (gpio_mmap_ptr == MAP_FAILED || gpio_mmap_ptr == NULL) {
        printf("[GPIO] mmap failed. Direct GPIO input disabled.\n");
        gpio_mmap_ptr = NULL;
        if (gpio_mem_fd >= 0) {
            close(gpio_mem_fd);
            gpio_mem_fd = -1;
        }
        g_gpio_cfg.enabled = 0;
        return;
    }

    // Configure all assigned pins as inputs
    set_gpio_input(g_gpio_cfg.up);
    set_gpio_input(g_gpio_cfg.down);
    set_gpio_input(g_gpio_cfg.left);
    set_gpio_input(g_gpio_cfg.right);
    set_gpio_input(g_gpio_cfg.a);
    set_gpio_input(g_gpio_cfg.b);
    set_gpio_input(g_gpio_cfg.x);
    set_gpio_input(g_gpio_cfg.y);
    set_gpio_input(g_gpio_cfg.l);
    set_gpio_input(g_gpio_cfg.r);
    set_gpio_input(g_gpio_cfg.start);
    set_gpio_input(g_gpio_cfg.select);
    set_gpio_input(g_gpio_cfg.quit);

    printf("[GPIO] Direct memory-mapped GPIO input initialized successfully (using /dev/gpiomem).\n");
}

uint32_t gpio_buttons_read(int *quit_requested)
{
    if (!g_gpio_cfg.enabled || !gpio_mmap_ptr) {
        if (quit_requested) *quit_requested = 0;
        return 0;
    }

    // GPLEV0 is word offset 13 (byte offset 0x34)
    uint32_t gplev = gpio_mmap_ptr[13];
    uint32_t val = 0;

    // Active-low: button pressed connects GPIO pin to GND (0 = pressed)
    #define POLL_PIN(pin_num, mask) \
        if ((pin_num) >= 0 && (pin_num) < 32 && !(gplev & (1u << (pin_num)))) val |= (mask)

    POLL_PIN(g_gpio_cfg.up,     SNES_UP_MASK);
    POLL_PIN(g_gpio_cfg.down,   SNES_DOWN_MASK);
    POLL_PIN(g_gpio_cfg.left,   SNES_LEFT_MASK);
    POLL_PIN(g_gpio_cfg.right,  SNES_RIGHT_MASK);
    POLL_PIN(g_gpio_cfg.a,      SNES_A_MASK);
    POLL_PIN(g_gpio_cfg.b,      SNES_B_MASK);
    POLL_PIN(g_gpio_cfg.x,      SNES_X_MASK);
    POLL_PIN(g_gpio_cfg.y,      SNES_Y_MASK);
    POLL_PIN(g_gpio_cfg.l,      SNES_TL_MASK);
    POLL_PIN(g_gpio_cfg.r,      SNES_TR_MASK);
    POLL_PIN(g_gpio_cfg.start,  SNES_START_MASK);
    POLL_PIN(g_gpio_cfg.select, SNES_SELECT_MASK);

    #undef POLL_PIN

    if (quit_requested) {
        if (g_gpio_cfg.quit >= 0 && g_gpio_cfg.quit < 32 && !(gplev & (1u << g_gpio_cfg.quit))) {
            *quit_requested = 1;
        } else if ((val & SNES_START_MASK) && (val & SNES_SELECT_MASK)) {
            *quit_requested = 1;
        } else {
            *quit_requested = 0;
        }
    }

    return val;
}

void gpio_buttons_close(void)
{
    if (gpio_mmap_ptr && gpio_mmap_ptr != MAP_FAILED) {
        munmap((void *)gpio_mmap_ptr, 4096);
        gpio_mmap_ptr = NULL;
    }
    if (gpio_mem_fd >= 0) {
        close(gpio_mem_fd);
        gpio_mem_fd = -1;
    }
}
