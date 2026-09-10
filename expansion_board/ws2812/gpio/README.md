# ws2812_drv

编译驱动前需要根据目标芯片，修改`ws2812_drv.c`中的芯片宏定义。

加载驱动程序和内核调试信息：

1. ws2812_drv.ko

```bash

# insmod ws2812_drv.ko

    [  376.312216] Load the ws2812 module successfully!
```

程序运行前需要修改ws2812_app.c使用的实际引脚定义。

编译应用程序后控制LED1的显示颜色，8个颜色循环显示：

```bash

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
