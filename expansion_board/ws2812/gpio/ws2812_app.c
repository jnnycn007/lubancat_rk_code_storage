/*
*
*   file: ws2812_app.c
*   date: 2024-09-03
*   usage: 
*       sudo gcc -o ws2812_app ws2812_app.c
*       sudo ./ws2812_app 1 ff0000        # 设置指定颜色
*       sudo ./ws2812_app 1              # 不传颜色参数，循环显示各种颜色
*
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <signal.h> 

#define WS2812_DATA_GPIOCHIP     3
#define WS2812_DATA_GPIONUM      19

/* 循环显示的颜色序列 (RGB) */
static const char *color_list[] = {
    "FF0000",   /* 红 */
    "00FF00",   /* 绿 */
    "0000FF",   /* 蓝 */
    "FFFFFF",   /* 白 */
    "00FFFF",   /* 青 */
    "FF00FF",   /* 品红 */
    "FFFF00",   /* 黄 */
    "FF8000",   /* 橙 */
    "8000FF",   /* 紫 */
    "00FF80",   /* 青绿 */
};
#define COLOR_NUM (sizeof(color_list) / sizeof(color_list[0]))

static int running = 1;

static void sig_handler(int sig)
{
    running = 0;
}

struct ws2812_mes {
    unsigned int gpiochip;      // data引脚的gpiochip
    unsigned int gpionum;       // data引脚的gpionum
    unsigned int lednum;        // 要控制灯带的第几个LED，序号从1开始
    unsigned char color[3];     // color[0]:color[1]:color[2]   R:G:B 
};

static int set_color(int fd, int lednum, const char *hex_color)
{
    struct ws2812_mes ws2812;

    ws2812.gpiochip = WS2812_DATA_GPIOCHIP;
    ws2812.gpionum  = WS2812_DATA_GPIONUM;
    ws2812.lednum   = lednum;

    if (sscanf(hex_color, "%2hhx%2hhx%2hhx",
               &ws2812.color[0], &ws2812.color[1], &ws2812.color[2]) != 3) {
        printf("Error: Invalid hex color format: %s\n", hex_color);
        return -1;
    }

    return write(fd, &ws2812, sizeof(struct ws2812_mes));
}

int main(int argc, char **argv)
{
    int fd;
    int lednum;
    char *endptr;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (argc < 2 || argc > 3)
    {
        printf("Usage: %s <led num> [hex_color]\n", argv[0]);
        printf("  指定颜色: %s 3 FF0000\n", argv[0]);
        printf("  循环变色: %s 3\n", argv[0]);
        return -1;
    }

    /* 参数1检查 */
    lednum = (int)strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || lednum < 1 || lednum > 20) {
        printf("Error: The first argument must be a number between 1 and 20.\n");
        return -1;
    }

    /* 打开ws2812设备节点 */
    fd = open("/dev/ws2812", O_RDWR);
    if (fd == -1)
    {
        printf("can not open file /dev/ws2812\n");
        return -1;
    }

    if (argc == 3)
    {
        /* 指定颜色模式 */
        if (strlen(argv[2]) != 6)
        {
            printf("Error: The second argument has illegal length.\n");
            close(fd);
            return -1;
        }
        set_color(fd, lednum, argv[2]);
    }
    else
    {
        /* 循环变色模式 */
        int idx = 0;
        printf("循环显示颜色中，按 Ctrl+C 退出...\n");
        while (running)
        {
            set_color(fd, lednum, color_list[idx]);
            printf("LED%d: %s\n", lednum, color_list[idx]);
            idx = (idx + 1) % COLOR_NUM;
            usleep(500000);  /* 500ms 切换一次 */
        }
        /* 退出时熄灭 */
        set_color(fd, lednum, "000000");
        printf("\n已熄灭，退出\n");
    }

    close(fd);
    return 0;
}