#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>

/* 定义RAM块设备的名称 */
#define RAMDISK_NAME        "ram_block"
/* 定义RAM磁盘的默认大小为16MB */
#define RAMDISK_SIZE_MB     16
/* 定义扇区大小为512字节 */
#define KERNEL_SECTOR_SIZE  512

/* 驱动私有数据结构体 */
struct ramdisk_dev {
    u8                  *data;      /* 指向RAM磁盘数据缓冲区的指针，存储实际的磁盘数据 */
    size_t              size;       /* RAM磁盘的总大小，单位为字节 */
    sector_t            capacity;   /* RAM磁盘的总容量，单位为扇区数 */
    struct gendisk      *gd;        /* 指向关联的gendisk结构体 */
    struct blk_mq_tag_set tag_set;  /* blk-mq标签集，管理多队列的请求标签和资源 */
    spinlock_t          lock;       /* 自旋锁，保护设备数据缓冲区的并发访问 */
    atomic_t            refcnt;     /* 原子引用计数，跟踪设备的打开次数 */
};

/* 全局设备指针 */
static struct ramdisk_dev *ramdisk_dev;

/* 动态分配的主设备号 */
static int major_num;

/* 块设备打开操作函数 */
static int ramdisk_open(struct block_device *bdev, fmode_t mode)
{
    /* 从块设备的gendisk结构体中获取驱动私有数据 */
    struct ramdisk_dev *dev = bdev->bd_disk->private_data;
    
    /* 原子递增设备引用计数 */
    atomic_inc(&dev->refcnt);
    /* 打印调试信息，显示当前引用计数 */
    pr_debug("ramdisk: Device opened, refcnt=%d\n", atomic_read(&dev->refcnt));

    return 0;
}

/* 块设备释放操作函数 */
static void ramdisk_release(struct gendisk *disk, fmode_t mode)
{
    /* 从gendisk结构体中获取驱动私有数据 */
    struct ramdisk_dev *dev = disk->private_data;
    
    /* 原子递减设备引用计数 */
    atomic_dec(&dev->refcnt);
    /* 打印调试信息，显示当前引用计数 */
    pr_debug("ramdisk: Device released, refcnt=%d\n", atomic_read(&dev->refcnt));
}

/* 获取磁盘几何信息函数 */
static int ramdisk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    /* 从块设备的gendisk结构体中获取驱动私有数据 */
    struct ramdisk_dev *dev = bdev->bd_disk->private_data;
    /* 获取设备总扇区数 */
    unsigned long capacity = dev->capacity;
    
    /* 模拟磁头数，设置为16 */
    geo->heads = 16;
    /* 模拟每磁道扇区数，设置为63 */
    geo->sectors = 63;
    /* 计算并模拟柱面数：总扇区数 / (磁头数 * 每磁道扇区数) */
    geo->cylinders = (unsigned short)(capacity / (geo->heads * geo->sectors));
    /* 磁盘起始扇区，设置为0 */
    geo->start = 0;
    
    return 0;
}

/* 块设备IOCTL控制操作函数 */
static int ramdisk_ioctl(struct block_device *bdev, fmode_t mode,
                        unsigned int cmd, unsigned long arg)
{
    /* 从块设备的gendisk结构体中获取驱动私有数据 */
    struct ramdisk_dev *dev = bdev->bd_disk->private_data;
    
    /* 根据不同的IOCTL命令进行处理 */
    switch (cmd) {
    /* BLKGETSIZE命令：获取设备总扇区数 */
    case BLKGETSIZE:
        /* 将设备容量复制到用户空间 */
        return put_user((unsigned long)dev->capacity, (unsigned long __user *)arg);
    /* BLKGETSIZE64命令：获取设备总字节数 */
    case BLKGETSIZE64:
        /* 将设备总字节数复制到用户空间 */
        return put_user((u64)dev->capacity << 9, (u64 __user *)arg);
    /* 不支持的IOCTL命令 */
    default:
        /* 返回不支持的命令错误码 */
        return -ENOTTY;
    }
}

/* 块设备操作函数集 */
static const struct block_device_operations ramdisk_fops = {
    .owner      = THIS_MODULE,
    .open       = ramdisk_open,
    .release    = ramdisk_release,
    .ioctl      = ramdisk_ioctl,
    .getgeo     = ramdisk_getgeo,   /* 获取磁盘几何信息函数指针 */
};

/* blk-mq请求处理主函数 */
static blk_status_t ramdisk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                     const struct blk_mq_queue_data *bd)
{
    /* 获取当前要处理的请求结构体 */
    struct request *rq = bd->rq;
    /* 从请求队列的私有数据中获取设备结构体 */
    struct ramdisk_dev *dev = rq->q->queuedata;
    /* 请求迭代器，用于遍历请求中的所有bio段 */
    struct req_iterator iter;
    /* bio向量结构体，存储单个数据段的信息 */
    struct bio_vec bvec;
    /* 当前处理的起始扇区号 */
    sector_t sector = blk_rq_pos(rq);
    /* 用于保存中断状态的变量 */
    unsigned long flags;
    /* 请求处理状态，初始化为成功 */
    blk_status_t status = BLK_STS_OK;
    
    /* 标记请求开始处理，更新请求状态 */
    blk_mq_start_request(rq);
    
    /* 检查是否为透传请求 */
    if (blk_rq_is_passthrough(rq)) {
        /* 打印警告信息，不支持透传请求 */
        pr_warn("ramdisk: Passthrough requests not supported\n");
        /* 设置状态为不支持 */
        status = BLK_STS_NOTSUPP;
        /* 跳转到请求完成处理 */
        goto out;
    }
    
    /* 获取自旋锁并禁用本地中断，保护数据缓冲区 */
    spin_lock_irqsave(&dev->lock, flags);
    
    /* 遍历请求中的所有bio数据段 */
    rq_for_each_segment(bvec, rq, iter) {
        /* 获取数据段在内存中的虚拟地址 */
        void *buf = page_address(bvec.bv_page) + bvec.bv_offset;
        /* 获取数据段的长度，单位为字节 */
        size_t len = bvec.bv_len;
        /* 计算数据段在RAM磁盘中的偏移量 */
        loff_t offset = (loff_t)sector << 9;
        
        /* 边界检查：确保IO操作不超出设备范围 */
        if (offset + len > dev->size) {
            /* 打印错误信息，显示越界的IO参数 */
            pr_err("ramdisk: I/O beyond device end (offset=%lld, len=%zu, size=%zu)\n",
                   offset, len, dev->size);
            /* 设置状态为IO错误 */
            status = BLK_STS_IOERR;
            /* 跳出循环，终止处理 */
            break;
        }
        
        /* 根据请求方向处理读写操作 */
        if (rq_data_dir(rq) == READ) {
            /* 读操作：从RAM磁盘复制数据到内核缓冲区 */
            memcpy(buf, dev->data + offset, len);
        } else {
            /* 写操作：从内核缓冲区复制数据到RAM磁盘 */
            memcpy(dev->data + offset, buf, len);
        }
        
        /* 更新下一个要处理的扇区号 */
        sector += len >> 9;
    }
    
    /* 释放自旋锁并恢复中断状态 */
    spin_unlock_irqrestore(&dev->lock, flags);

out:
    /* 完成请求处理，通知blk-mq框架 */
    blk_mq_end_request(rq, status);
    /* 返回处理状态 */
    return BLK_STS_OK;
}

/* blk-mq操作函数集 */
static const struct blk_mq_ops ramdisk_mq_ops = {
    /* 请求处理函数指针，blk-mq的入口 */
    .queue_rq = ramdisk_queue_rq,
};

/* 初始化RAM磁盘设备 */
static int __init ramdisk_init_device(struct ramdisk_dev *dev)
{
    /* 函数返回值 */
    int ret;
    /* 硬件队列数量 */
    unsigned int hw_queues;
    
    /* 计算设备总大小：MB转换为字节 */
    dev->size = RAMDISK_SIZE_MB * 1024 * 1024;
    /* 计算设备总容量：总字节数 / 扇区大小 */
    dev->capacity = dev->size / KERNEL_SECTOR_SIZE;
    
    /* 分配RAM磁盘数据缓冲区，使用vzalloc分配大内存并清零 */
    dev->data = vzalloc(dev->size);
    if (!dev->data) {
        /* 打印错误信息，显示分配失败的内存大小 */
        pr_err("ramdisk: Failed to allocate %zu bytes of memory\n", dev->size);
        /* 返回内存不足错误码 */
        return -ENOMEM;
    }
    
    /* 初始化保护数据的自旋锁 */
    spin_lock_init(&dev->lock);
    /* 初始化设备引用计数为0 */
    atomic_set(&dev->refcnt, 0);
    
    /* 硬件队列数固定等于系统在线CPU数量 */
    hw_queues = num_online_cpus();
    /* 打印信息，显示使用的硬件队列数量 */
    pr_info("ramdisk: Using %u hardware queues\n", hw_queues);
    
    /* 初始化blk-mq标签集结构体，清零所有字段 */
    memset(&dev->tag_set, 0, sizeof(dev->tag_set));
    /* 设置blk-mq操作函数集 */
    dev->tag_set.ops = &ramdisk_mq_ops;
    /* 设置硬件队列数量 */
    dev->tag_set.nr_hw_queues = hw_queues;
    /* 设置每个硬件队列的最大请求深度 */
    dev->tag_set.queue_depth = 128;
    /* 设置NUMA节点为无特定节点 */
    dev->tag_set.numa_node = NUMA_NO_NODE;
    /* 设置blk-mq标志：允许请求合并，提高性能 */
    dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    /* 设置标签集的驱动私有数据，指向设备结构体 */
    dev->tag_set.driver_data = dev;
    
    /* 分配blk-mq标签集资源 */
    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret) {
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to allocate blk-mq tag set\n");
        /* 跳转到错误处理，释放已分配的数据缓冲区 */
        goto free_data;
    }

/* 内核版本判断：Linux 5.14及以上版本使用新API */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
    /* 使用blk_mq_alloc_disk一步分配gendisk和请求队列 */
    dev->gd = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (IS_ERR(dev->gd)) {
        /* 获取错误码 */
        ret = PTR_ERR(dev->gd);
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to allocate gendisk\n");
        /* 跳转到错误处理，释放已分配的标签集 */
        goto free_tag_set;
    }

/* 内核版本判断：Linux 4.19-5.13版本使用传统API */
#else
    /* 先分配gendisk结构体，参数为支持的次设备号数量 */
    dev->gd = alloc_disk(1);
    if (!dev->gd) {
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to allocate gendisk\n");
        /* 设置错误码为内存不足 */
        ret = -ENOMEM;
        /* 跳转到错误处理，释放已分配的标签集 */
        goto free_tag_set;
    }
    
    /* 初始化blk-mq请求队列 */
    dev->gd->queue = blk_mq_init_queue(&dev->tag_set);
    if (IS_ERR(dev->gd->queue)) {
        /* 获取错误码 */
        ret = PTR_ERR(dev->gd->queue);
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to initialize blk-mq queue\n");
        /* 跳转到错误处理，释放已分配的gendisk */
        goto put_disk;
    }
    
    /* 设置请求队列的私有数据，指向设备结构体 */
    dev->gd->queue->queuedata = dev;
#endif

    /* 设置gendisk的主设备号 */
    dev->gd->major = major_num;
    /* 设置gendisk的第一个次设备号 */
    dev->gd->first_minor = 0;
    /* 设置gendisk支持的次设备号数量 */
    dev->gd->minors = 1;
    /* 设置gendisk的块设备操作函数集 */
    dev->gd->fops = &ramdisk_fops;
    /* 设置gendisk的私有数据，指向设备结构体 */
    dev->gd->private_data = dev;
    /* 设置gendisk的设备名称 */
    snprintf(dev->gd->disk_name, DISK_NAME_LEN, RAMDISK_NAME);
    
    /* 设置设备容量 */
    set_capacity(dev->gd, dev->capacity);
    /* 设置队列的逻辑块大小 */
    blk_queue_logical_block_size(dev->gd->queue, KERNEL_SECTOR_SIZE);
    /* 设置队列的物理块大小 */
    blk_queue_physical_block_size(dev->gd->queue, KERNEL_SECTOR_SIZE);
    /* 设置队列支持的最大硬件扇区数 */
    blk_queue_max_hw_sectors(dev->gd->queue, 1024);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    /* Linux 5.15及以上版本add_disk有返回值 */
    ret = add_disk(dev->gd);
    if (ret) {
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to add disk: %d\n", ret);
        /* 跳转到错误处理，释放已分配的gendisk */
        goto put_disk;
    }
#else
    /* Linux 4.19-5.14版本add_disk返回void，不会失败 */
    add_disk(dev->gd);

    /* 设置返回值为成功 */
    ret = 0;
#endif

    /* 打印设备注册成功信息 */
    pr_info("ramdisk: Device registered successfully\n");
    /* 打印设备大小信息 */
    pr_info("ramdisk: Size: %d MB (%zu bytes, %llu sectors)\n",
           RAMDISK_SIZE_MB, dev->size, (unsigned long long)dev->capacity);
    
    return 0;

/* 释放gendisk结构体 */
put_disk:
    put_disk(dev->gd);
/* 释放blk-mq标签集 */
free_tag_set:
    blk_mq_free_tag_set(&dev->tag_set);
/* 释放RAM数据缓冲区 */
free_data:
    vfree(dev->data);

    return ret;
}

/* 清理RAM磁盘设备 */
static void ramdisk_cleanup_device(struct ramdisk_dev *dev)
{
    /* 检查设备指针是否有效 */
    if (!dev)
        return;
    
    /* 检查gendisk是否已分配 */
    if (dev->gd) {
        /* 从系统中删除块设备 */
        del_gendisk(dev->gd);
/* 内核版本判断：4.19-5.17版本需要显式清理队列 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
        /* 清理blk-mq请求队列 */
        blk_cleanup_queue(dev->gd->queue);
#endif
        /* 释放gendisk结构体 */
        put_disk(dev->gd);
    }
    
    /* 释放blk-mq标签集 */
    blk_mq_free_tag_set(&dev->tag_set);
    
    /* 检查数据缓冲区是否已分配 */
    if (dev->data) {
        /* 释放RAM数据缓冲区 */
        vfree(dev->data);
        /* 将指针置空，防止野指针 */
        dev->data = NULL;
    }
    
    /* 释放设备私有数据结构体 */
    kfree(dev);
}

/* 模块初始化函数 */
static int __init ramdisk_init(void)
{
    /* 函数返回值 */
    int ret;
    /* 内核版本号的三个组成部分 */
    unsigned int major, minor, patch;
    
    /* 从编译时内核版本号中解析主版本号 */
    major = (LINUX_VERSION_CODE >> 16) & 0xFF;
    /* 解析次版本号 */
    minor = (LINUX_VERSION_CODE >> 8) & 0xFF;
    /* 解析补丁版本号 */
    patch = LINUX_VERSION_CODE & 0xFF;
    
    /* 动态分配块设备主设备号 */
    major_num = register_blkdev(0, RAMDISK_NAME);
    if (major_num < 0) {
        /* 打印错误信息 */
        pr_err("ramdisk: Failed to register block device\n");
        /* 返回错误码 */
        return major_num;
    }
    
    /* 分配设备私有数据结构体，使用kzalloc分配并清零 */
    ramdisk_dev = kzalloc(sizeof(struct ramdisk_dev), GFP_KERNEL);
    if (!ramdisk_dev) {
        /* 设置错误码为内存不足 */
        ret = -ENOMEM;
        /* 跳转到错误处理，注销已分配的主设备号 */
        goto unregister_blkdev;
    }
    
    /* 初始化RAM磁盘设备 */
    ret = ramdisk_init_device(ramdisk_dev);
    if (ret) {
        /* 跳转到错误处理，释放已分配的设备结构体 */
        goto free_dev;
    }
    
    /* 打印模块加载成功信息 */
    pr_info("ramdisk: Module loaded successfully\n");

    return 0;

/* 释放设备私有数据结构体 */
free_dev:
    kfree(ramdisk_dev);
/* 注销块设备主设备号 */
unregister_blkdev:
    unregister_blkdev(major_num, RAMDISK_NAME);

    return ret;
}

/* 模块退出函数 */
static void __exit ramdisk_exit(void)
{
    /* 清理RAM磁盘设备所有资源 */
    ramdisk_cleanup_device(ramdisk_dev);
    /* 注销块设备主设备号 */
    unregister_blkdev(major_num, RAMDISK_NAME);
    /* 打印模块卸载成功信息 */
    pr_info("ramdisk: Module unloaded successfully\n");
}

/* 注册模块初始化函数 */
module_init(ramdisk_init);
/* 注册模块退出函数 */
module_exit(ramdisk_exit);

MODULE_AUTHOR("embedfire <embedfire@embedfire.com>");
MODULE_DESCRIPTION("ram_block module");
MODULE_LICENSE("GPL v2");