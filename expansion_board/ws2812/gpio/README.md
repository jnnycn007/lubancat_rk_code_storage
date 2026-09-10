# ws2812_drv

编译驱动前需要根据目标芯片，修改`ws2812_drv.c`中的芯片宏定义。

加载驱动程序和内核调试信息：

1. ws2812_drv.ko

```bash

# insmod ws2812_drv.ko

    [  376.312216] Load the ws2812 module successfully!
```

程序运行前需要修改ws2812_app.c使用的实际引脚定义。

编译应用程序后需要先调整cpu为performance模式，将cpu频率设置为最高频率，然后才能运行程序控制LED的显示颜色

如控制LED1的显示颜色，8个颜色循环显示：

```bash

# sudo sh -c "echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor"

# ./ws2812_app 1

    循环显示颜色中，按 Ctrl+C 退出...
    LED1: FF0000
    LED1: 00FF00
    LED1: 0000FF
    LED1: FFFFFF
    LED1: 00FFFF
    LED1: FF00FF
    ^C
    已熄灭，退出
```

控制LED1指定显示颜色，如红色：

```bash

# ./ws2812_app 1 FF0000
```
