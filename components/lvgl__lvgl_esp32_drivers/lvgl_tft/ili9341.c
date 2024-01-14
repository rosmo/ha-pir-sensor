/**
 * @file ili9341.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "ili9341.h"

#include "disp_spi.h"
#include "display_port.h"

/*********************
 *      DEFINES
 *********************/
#define END_OF_CMD_MARKER           0xFFU

#define MEMORY_ACCESS_CONTROL_REG   0x36U
#define SOFTWARE_RESET_REG          0x01U

/**********************
 *      TYPEDEFS
 **********************/

/*The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct. */

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void ili9341_set_orientation(lv_disp_drv_t * drv, uint8_t orientation);

static void ili9341_send_cmd(lv_disp_drv_t * drv, uint8_t cmd);
static void ili9341_send_data(lv_disp_drv_t * drv, void * data, uint16_t length);
static void ili9341_send_color(lv_disp_drv_t * drv, void * data, uint16_t length);

static void ili9341_reset(lv_disp_drv_t * drv);
/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ili9341_init(lv_disp_drv_t * drv)
{
    lcd_init_cmd_t ili_init_cmds[] = {
        {0xCF, {0x00, 0x83, 0X30}, 3},
        {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
        {0xE8, {0x85, 0x01, 0x79}, 3},
        {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
        {0xF7, {0x20}, 1},
        {0xEA, {0x00, 0x00}, 2},
        /* Power control */
        {0xC0, {0x26}, 1},
        /* Power control */
        {0xC1, {0x11}, 1},
        /* VCOM control */
        {0xC5, {0x35, 0x3E}, 2},
        /* VCOM control */
        {0xC7, {0xBE}, 1},
        /* Memory Access Control */
        {0x36, {0x28}, 1},
        /* Pixel Format Set */
        {0x3A, {0x55}, 1},
        {0xB1, {0x00, 0x1B}, 2},
        {0xF2, {0x08}, 1},
        {0x26, {0x01}, 1},
        {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
        {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
        {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
        {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
        {0x2C, {0}, 0},
        {0xB7, {0x07}, 1},
        {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
        {0x11, {0}, 0x80},
        {0x29, {0}, 0x80},
        {0, {0}, END_OF_CMD_MARKER},
    };

    ili9341_reset(drv);

    //Send all the commands
    uint16_t cmd = 0;
    while (ili_init_cmds[cmd].databytes != END_OF_CMD_MARKER) {
        ili9341_send_cmd(drv, ili_init_cmds[cmd].cmd);
        ili9341_send_data(drv, ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes & 0x1F);
        
        if (ili_init_cmds[cmd].databytes & 0x80) {
            display_port_delay(drv, 100);
        }

        cmd++;
    }

    ili9341_set_orientation(drv, ILI9341_INITIAL_ORIENTATION);

#if ILI9341_INVERT_COLORS == 1U
    ili9341_send_cmd(drv, 0x21);
#else
    ili9341_send_cmd(drv, 0x20);
#endif
}


void ili9341_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
    uint8_t data[4] = {0};
    uint32_t size = lv_area_get_width(area) * lv_area_get_height(area);

    /*Column addresses*/
    data[0] = (area->x1 >> 8) & 0xFF;
    data[1] = area->x1 & 0xFF;
    data[2] = (area->x2 >> 8) & 0xFF;
    data[3] = area->x2 & 0xFF;

    ili9341_send_cmd(drv, 0x2A);
    ili9341_send_data(drv, data, 4);

    /* Page addresses */
    data[0] = (area->y1 >> 8) & 0xFF;
    data[1] = area->y1 & 0xFF;
    data[2] = (area->y2 >> 8) & 0xFF;
    data[3] = area->y2 & 0xFF;

    ili9341_send_cmd(drv, 0x2B);
    ili9341_send_data(drv, data, 4);

    /* Memory write */
    ili9341_send_cmd(drv, 0x2C);
    ili9341_send_color(drv, (void*)color_map, size * 2);
}

void ili9341_sleep_in(lv_disp_drv_t * drv)
{
    uint8_t data[] = {0x08};
    ili9341_send_cmd(drv, 0x10);
    ili9341_send_data(drv, data, 1);
}

void ili9341_sleep_out(lv_disp_drv_t * drv)
{
    uint8_t data[] = {0x08};
    ili9341_send_cmd(drv, 0x11);
    ili9341_send_data(drv, data, 1);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static inline void set_cmd_mode(lv_disp_drv_t * drv)
{
    display_port_gpio_dc(drv, 0);
}

static inline void set_data_mode(lv_disp_drv_t * drv)
{
    display_port_gpio_dc(drv, 1);
}

static void ili9341_send_cmd(lv_disp_drv_t * drv, uint8_t cmd)
{
    disp_wait_for_pending_transactions();
    set_cmd_mode(drv);
    disp_spi_send_data(&cmd, 1);
}

static void ili9341_send_data(lv_disp_drv_t *drv, void * data, uint16_t length)
{
    disp_wait_for_pending_transactions();
    set_data_mode(drv);
    disp_spi_send_data(data, length);
}

static void ili9341_send_color(lv_disp_drv_t *drv, void * data, uint16_t length)
{
    disp_wait_for_pending_transactions();
    set_data_mode(drv);
    disp_spi_send_colors(data, length);
}

static void ili9341_set_orientation(lv_disp_drv_t *drv, uint8_t orientation)
{
    assert(orientation < 4);

#if defined CONFIG_LV_PREDEFINED_DISPLAY_M5STACK
    const uint8_t data[] = {0x68, 0x68, 0x08, 0x08};
#elif defined (CONFIG_LV_PREDEFINED_DISPLAY_M5CORE2)
	const uint8_t data[] = {0x08, 0x88, 0x28, 0xE8};
#elif defined (CONFIG_LV_PREDEFINED_DISPLAY_WROVER4)
    const uint8_t data[] = {0x6C, 0xEC, 0xCC, 0x4C};
#else
    const uint8_t data[] = {0x48, 0x88, 0x28, 0xE8};
#endif

    ili9341_send_cmd(drv, MEMORY_ACCESS_CONTROL_REG);
    ili9341_send_data(drv, (void *) &data[orientation], 1);
}

/* Reset the display, if we don't have a reset pin we use software reset */
static void ili9341_reset(lv_disp_drv_t *drv)
{
#if defined(ILI9341_USE_RST)
    display_port_gpio_rst(drv, 0);
    display_port_delay(drv, 100);
    display_port_gpio_rst(drv, 1);
    display_port_delay(drv, 100);
#else
    ili9341_send_cmd(drv, SOFTWARE_RESET_REG);
    display_port_delay(drv, 5);
#endif
}
