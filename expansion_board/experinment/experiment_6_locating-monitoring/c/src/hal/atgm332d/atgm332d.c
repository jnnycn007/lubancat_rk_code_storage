/*
*
*   file: atgm332d.c
*   update: 2024-09-13
*
*/

#include "uart.h"
#include "atgm332d.h"

static int fd_atgm332d;
static pthread_t atgm332d_thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

volatile static int thread_flag = 1;

// $GPRMC,111025.00,A,2517.033747,N,11019.176025,E,0.0,144.8,270920,2.3,W,A*2D\r\n
// $GPRMC,,V,,,,,,,,,,N*53\r\n
// $GPRMC,024443.0,A,2517.038296,N,11019.174048,E,0.0,,120201,0.0,E,A*2F\r\n
static char buf[500];
static char Utctime[20];
static char Lat[20]; 
static char ns[5]; 
static char Lng[20]; 
static char ew[5];
static char Date[20];

/*****************************
 * @brief : atgm332d读原始数据
 * @param : none
 * @return: 0成功 -1失败
*****************************/
static int atgm332d_read_raw_data()
{
	int i = 0;
	int ret;
	char c;
	int start = 0;
	
	while (1)
	{
		ret = read(fd_atgm332d, &c, 1);
		if (ret == 1)
		{
			if(c == '$')
				start = 1;
			if(start)
			    buf[i++] = c;
			if(c == '\n' || c == '\r')
            {
                buf[i] = '\0';
                return 0;
            }		
		}
		else
		    return -1;
	}
}

/*****************************
 * @brief : atgm332d解析原始数据
 * @param : none
 * @return: 0成功 -1失败
*****************************/
static int atgm332d_parse_raw_data()
{
	char head[10];
    char signal[10];
    char speed[10];
    char azimuth[10];

	if(buf[0] != '$')
		return -1;
	else if(strncmp(buf+3, "RMC", 3) != 0)
		return -1;
	else 
    {
        //$GPRMC,111025.00,A,2517.033747,N,11019.176025,E,0.0,144.8,270920,2.3,W,A*2D\r\n
        //printf("raw buff = %s\n", buf);
        sscanf(buf, "%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,],%[^,]", head, Utctime, signal, Lat, ns, Lng, ew, speed, azimuth, Date);
		return 0;
	}
}

/*****************************
 * @brief : atgm332d线程函数
 * @param : arg 函数参数
 * @return: none
*****************************/
static void *atgm332d_thread_read(void *arg) 
{  
    int ret = 0;

    while(thread_flag) 
    {  
        ret = atgm332d_read_raw_data();
        if(ret == 0)
		{
            pthread_mutex_lock(&lock);
			ret = atgm332d_parse_raw_data();
            pthread_mutex_unlock(&lock);
		}
    }   

    printf("atgm332d_thread has been stopped.\n");  
    pthread_exit(NULL);
}

/*****************************
 * @brief : atgm332d初始化
 * @param : com 设备dev
 * @return: 0成功 -1失败
*****************************/
int atgm332d_init(char *uart_dev)
{
    int ret;

    fd_atgm332d = open_port(uart_dev);
	if(fd_atgm332d < 0)
	{
		printf("open %s err!\n", uart_dev);
		return -1;
	}

    ret = set_opt(fd_atgm332d, 9600, 8, 'N', 1);
	if (ret)
	{
		printf("set port err!\n");
		return -1;
	}

	return 0;
}

/*****************************
 * @brief : atgm332d线程启动
 * @param : none
 * @return: 0成功 -1失败
*****************************/
int atgm332d_thread_start()
{
    int ret = 0;

    if(fd_atgm332d < 0) 
    {  
        fprintf(stderr, "atgm332d can not init!\n");  
        return -1;  
    }

    ret = pthread_create(&atgm332d_thread, NULL, atgm332d_thread_read, NULL);  

    return ret;
}

/*****************************
 * @brief : atgm332d线程停止
 * @param : none
 * @return: none
*****************************/
/* 暂不作处理 */

/*****************************
 * @brief : atgm332d反初始化
 * @param : none
 * @return: none
*****************************/
void atgm332d_exit()
{
    if(fd_atgm332d > 0)
        close(fd_atgm332d);
}

/*****************************
 * @brief : atgm332d获取经纬度数据
 * @param : lat 纬度
 * @param : lon 经度 
 * @return: none
*****************************/
void atgm332d_get_latlon_dd(double *lat, double *lon)
{
    double lat_dd, lon_dd;

    if(strcmp(Lat, "") == 0 || strcmp(Lng, "") == 0)
    {
        *lat = 00.0;
        *lon = 00.0;
        return;
    }

    sscanf(Lat, "%lf", &lat_dd);
    sscanf(Lng, "%lf", &lon_dd);

    lat_dd /= 100;
    lon_dd /= 100;

    // lat=22.537216, lon=113.507007
    // lat_dd = 22 + 53.7216/60
    // lon_dd = 113 + 50.7007/60
    lat_dd = (int)lat_dd + ((lat_dd - (int)lat_dd)*100) / 60;
    lon_dd = (int)lon_dd + ((lon_dd - (int)lon_dd)*100) / 60;

    *lat = lat_dd;
    *lon = lon_dd;
}

/*****************************
 * @brief : atgm332d获取北京时间
 * @param : year 年
 * @param : month 月
 * @param : day 日
 * @param : hours 时
 * @param : min 分
 * @return: none
*****************************/
void atgm332d_get_bjtime(unsigned int *year, unsigned int *month, unsigned int *day, unsigned int *hours, unsigned int *min)
{
    if(strcmp(Date, "") == 0 || strcmp(Utctime, "") == 0)
    {
        *year = 0;
        *month = 0;
        *day = 0;
        *hours = 0;
        *min = 0;
        return;
    }

    /* 时分 */
    *hours = (Utctime[0] - '0') * 10 + (Utctime[1] - '0');
    *hours += 8;
    if (*hours >= 24)  
        *hours -= 24;  
    *min = (Utctime[2] - '0') * 10 + (Utctime[3] - '0');

    /* 年月日 */
    *day = (Date[0] - '0') * 10 + (Date[1] - '0');  
    *month = (Date[2] - '0') * 10 + (Date[3] - '0');  
    *year = 2000 + (Date[4] - '0') * 10 + (Date[5] - '0');
}