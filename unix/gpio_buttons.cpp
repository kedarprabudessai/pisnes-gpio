#include "gpio_buttons.h"
#include <glib.h>
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

// BCM2835 GPIO register word offsets (within 4KB page at 0x20200000)
#define BCM_GPIO_GPLEV0     13  // 0x34
#define BCM_GPIO_GPPUD      37  // 0x94
#define BCM_GPIO_GPPUDCLK0  38  // 0x98

typedef struct {
    uint32_t pin_mask;
    uint32_t snes_mask;
} gpio_binding_t;

gpio_config_t g_gpio_cfg;
static volatile uint32_t *gpio_mmap_ptr = NULL;
static int gpio_mem_fd = -1;
static gpio_binding_t gpio_bindings[12];
static int num_gpio_bindings = 0;
static uint32_t gpio_quit_mask = 0;

static int get_gpio_conf(GKeyFile *keyfile, const char *option, int defval)
{
    if (!keyfile) return defval;
    GError *error = NULL;
    int val = g_key_file_get_integer(keyfile, "GPIO", option, &error);
    if (!error)
        return val;
    g_error_free(error);
    return defval;
}

static void add_binding(int pin, uint32_t snes_mask, uint32_t *all_pins_mask)
{
    if (pin < 0 || pin >= 32) return;
    // Configure pin as input in GPFSEL register (3 bits = 000)
    int reg_idx = pin / 10;
    int shift = (pin % 10) * 3;
    gpio_mmap_ptr[reg_idx] &= ~(7u << shift);

    uint32_t bit = (1u << pin);
    *all_pins_mask |= bit;
    if (num_gpio_bindings < (int)(sizeof(gpio_bindings) / sizeof(gpio_bindings[0]))) {
        gpio_bindings[num_gpio_bindings].pin_mask = bit;
        gpio_bindings[num_gpio_bindings].snes_mask = snes_mask;
        num_gpio_bindings++;
    }
}

void gpio_buttons_init(GKeyFile *keyfile)
{
    // Close any prior mapping so repeated init calls (e.g. GUI loop) never leak fds
    gpio_buttons_close();
    memset(&g_gpio_cfg, 0, sizeof(g_gpio_cfg));
    num_gpio_bindings = 0;
    gpio_quit_mask = 0;

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

    const char *dev_name = "/dev/gpiomem";
    gpio_mem_fd = open(dev_name, O_RDWR | O_SYNC);
    if (gpio_mem_fd < 0) {
        // Fallback to /dev/mem if /dev/gpiomem is unavailable (requires root)
        dev_name = "/dev/mem";
        gpio_mem_fd = open(dev_name, O_RDWR | O_SYNC);
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
        close(gpio_mem_fd);
        gpio_mem_fd = -1;
        g_gpio_cfg.enabled = 0;
        return;
    }

    // Configure all assigned pins as inputs and build active-low polling table
    uint32_t all_pins_mask = 0;
    add_binding(g_gpio_cfg.up,     SNES_UP_MASK,     &all_pins_mask);
    add_binding(g_gpio_cfg.down,   SNES_DOWN_MASK,   &all_pins_mask);
    add_binding(g_gpio_cfg.left,   SNES_LEFT_MASK,   &all_pins_mask);
    add_binding(g_gpio_cfg.right,  SNES_RIGHT_MASK,  &all_pins_mask);
    add_binding(g_gpio_cfg.a,      SNES_A_MASK,      &all_pins_mask);
    add_binding(g_gpio_cfg.b,      SNES_B_MASK,      &all_pins_mask);
    add_binding(g_gpio_cfg.x,      SNES_X_MASK,      &all_pins_mask);
    add_binding(g_gpio_cfg.y,      SNES_Y_MASK,      &all_pins_mask);
    add_binding(g_gpio_cfg.l,      SNES_TL_MASK,     &all_pins_mask);
    add_binding(g_gpio_cfg.r,      SNES_TR_MASK,     &all_pins_mask);
    add_binding(g_gpio_cfg.start,  SNES_START_MASK,  &all_pins_mask);
    add_binding(g_gpio_cfg.select, SNES_SELECT_MASK, &all_pins_mask);

    if (g_gpio_cfg.quit >= 0 && g_gpio_cfg.quit < 32) {
        int reg_idx = g_gpio_cfg.quit / 10;
        int shift = (g_gpio_cfg.quit % 10) * 3;
        gpio_mmap_ptr[reg_idx] &= ~(7u << shift);
        gpio_quit_mask = (1u << g_gpio_cfg.quit);
        all_pins_mask |= gpio_quit_mask;
    }

    // Enable BCM2835 internal pull-up resistors for all active button pins
    // (Belt-and-suspenders alongside /boot/config.txt gpio=...=pu)
    if (all_pins_mask) {
        gpio_mmap_ptr[BCM_GPIO_GPPUD] = 2; // 2 = Pull-Up
        usleep(10);
        gpio_mmap_ptr[BCM_GPIO_GPPUDCLK0] = all_pins_mask;
        usleep(10);
        gpio_mmap_ptr[BCM_GPIO_GPPUD] = 0;
        gpio_mmap_ptr[BCM_GPIO_GPPUDCLK0] = 0;
    }

    printf("[GPIO] Direct memory-mapped GPIO input initialized (%s, %d buttons).\n",
           dev_name, num_gpio_bindings);
}

uint32_t gpio_buttons_read(int *quit_requested)
{
    if (!g_gpio_cfg.enabled || !gpio_mmap_ptr) {
        if (quit_requested) *quit_requested = 0;
        return 0;
    }

    // Read all 32 GPIO levels in a single bus transaction; active-low (0 = pressed)
    uint32_t pressed_bits = ~gpio_mmap_ptr[BCM_GPIO_GPLEV0];
    uint32_t val = 0;

    for (int i = 0; i < num_gpio_bindings; i++) {
        if (pressed_bits & gpio_bindings[i].pin_mask) {
            val |= gpio_bindings[i].snes_mask;
        }
    }

    if (quit_requested) {
        if ((gpio_quit_mask && (pressed_bits & gpio_quit_mask)) ||
            ((val & (SNES_START_MASK | SNES_SELECT_MASK)) == (SNES_START_MASK | SNES_SELECT_MASK))) {
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
