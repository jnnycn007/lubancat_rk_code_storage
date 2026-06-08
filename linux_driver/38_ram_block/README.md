# ram_block

运行`make`命令后，将会有一个模块：

* ram_block.ko

加载驱动程序和内核调试信息：

1. ram_block.ko

```bash
#加载驱动
sudo insmod ram_block.ko

#信息输出如下
[   67.274483] ramdisk: Using 4 hardware queues
[   67.276909] ramdisk: Device registered successfully
[   67.276968] ramdisk: Size: 16 MB (16777216 bytes, 32768 sectors)
[   67.276978] ramdisk: Module loaded successfully

#查看系统所有块设备
lsblk

#信息输出如下
NAME         MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
mmcblk0      179:0    0  7.3G  0 disk
├─mmcblk0p1  179:1    0    8M  0 part
├─mmcblk0p2  179:2    0  128M  0 part /boot
└─mmcblk0p3  179:3    0  7.2G  0 part /
mmcblk0boot0 179:32   0    4M  1 disk
mmcblk0boot1 179:64   0    4M  1 disk
ram_block    252:0    0   16M  0 disk

#查看磁盘详细信息
sudo fdisk -l /dev/ram_block

#信息输出如下
Disk /dev/ram_block: 16 MiB, 16777216 bytes, 32768 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes

#格式化为ext4文件系统
sudo mkfs.ext4 /dev/ram_block

#信息输出如下
mke2fs 1.44.5 (15-Dec-2018)
Creating filesystem with 16384 1k blocks and 4096 inodes
Filesystem UUID: fdf23f04-32ff-4f05-bd65-da578d27fd64
Superblock backups stored on blocks:
      8193

Allocating group tables: done
Writing inode tables: done
Creating journal (1024 blocks): done
Writing superblocks and filesystem accounting information: done

#挂载虚拟磁盘
sudo mount /dev/ram_block /mnt/

#信息输出如下
[  718.238154] EXT4-fs (ram_block): mounted filesystem with ordered data mode. Opts: (null)

#进入挂载目录
cd /mnt/

#写入测试文件
sudo sh -c "echo RAM Block Device Test > test.txt"

#读取测试文件
cat test.txt

#信息打印如下
RAM Block Device Test

#返回家目录
cd ~

#卸载挂载点
sudo umount /mnt/

#卸载内核模块
sudo rmmod ram_block

#信息打印如下
[  994.557159] ramdisk: Module unloaded successfully

#查看系统所有块设备
lsblk

#信息打印如下
NAME         MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
mmcblk0      179:0    0  7.3G  0 disk
├─mmcblk0p1  179:1    0    8M  0 part
├─mmcblk0p2  179:2    0  128M  0 part /boot
└─mmcblk0p3  179:3    0  7.2G  0 part /
mmcblk0boot0 179:32   0    4M  1 disk
mmcblk0boot1 179:64   0    4M  1 disk
```