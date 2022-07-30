/*************************************************************************
  > 完整实现了2048游戏的功能，并且添加了开机动画
 ************************************************************************/

#include <time.h> 
#include <stdio.h> 
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "freetype.h"

int* p_lcd = NULL;      // 屏幕映射内存的起始地址

int g_lcd_width = 800;  //lcd屏幕的宽
int g_lcd_high  = 480;  //lcd屏幕的高
int g_lcd_bpp   = 32;   //每个像素点所占比特位

//操作显示数组
char buf_f[30];
//步数纪录
char buf_s[30];
int step = 0;
//当前矩阵元素中最大值
char buf_m[30];
int max = 0;

//游戏结束标志
bool game_over = false;

#define MOVE_LEFT  1   // 左
#define MOVE_RIGHT 2   // 右
#define MOVE_UP    3   // 上
#define MOVE_DOWN  4   // 下

#define BOARDSIZE  4   // 整个棋盘的大小BORADSIZE * BOARDSIZE

struct header
{
	int16_t type;
	int32_t size;
	int16_t reserved1;
	int16_t reserved2;
	int32_t offbytes;
}__attribute__((packed));

struct info
{
	int32_t size;
	int32_t width;
	int32_t height;
	int16_t planes;

	int16_t bit_count;
	int32_t compression;
	int32_t size_img;
	int32_t xpel;
	int32_t ypel;
	int32_t clrused;
	int32_t partant;
}__attribute__((packed));

//将所有的图片名保存到一个数组中
const char * bmpfiles[] =	
{
	"res/2.bmp", 
	"res/4.bmp",
	"res/8.bmp",
	"res/16.bmp",
	"res/32.bmp",
	"res/64.bmp",
	"res/128.bmp",
	"res/256.bmp",
	"res/512.bmp",
	"res/1024.bmp",
	"res/2048.bmp",
	"res/4096.bmp",
	"res/8192.bmp",
	"res/16384.bmp",
	"res/32768.bmp",
	"res/65536.bmp",
};

//棋盘矩阵
int matrix[BOARDSIZE][BOARDSIZE] =
{
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0,0,0,0	
};

/*
get_bmpfiles_index:根据你要显示的数字(2,4,8,16,...)
	返回你对应的文件名的下标
返回值:
	返回 x对应的文件名在数组bmpfiles的下标
*/
int get_bmpfiles_index(int x)
{
	int exp;
	for(exp=0; x!=0; x >>= 1, exp++);

	return exp-2;
}

/*
	get_zero_num:求棋盘矩阵里面有多少个0
	返回值:
		返回棋盘矩阵中0的个数
*/
int get_zero_num(void)
{
	int z = 0;//棋盘矩阵中元素为0的个数
	int i, j;

	//BOARDSIZE = 4,整个棋盘大小为BOARDSIZE*BOARDSIZE
	for (i = 0; i < BOARDSIZE; i++)
	{
		for (j = 0; j < BOARDSIZE; j++)
		{
			if (matrix[i][j] == 0)
			{
				z++;
			}
		}
	}

	return z;
}

/*
	set_matrix:给棋盘矩阵第z个0的位置，填充一个
		值s
*/
void set_matrix(int z,  int s)
{
	int i, j;
	int k = 0 ;//0的个数

	for (i = 0; i < BOARDSIZE ;i++)
	{
		for (j = 0; j < BOARDSIZE; j++)
		{
			if (matrix[i][j] == 0)
			{
				k++;
				if (k == z)
				{
					matrix[i][j] = s;
					return ;
				}
			}
			
		}
	}
}

/*
 * x: x轴坐标
 * y: y轴坐标
 * color: 要填充的颜色
*/
void lcd_draw_point(int x, int y, int color)
{
    if (x >= 0 && x < g_lcd_width && y >=0 && y < g_lcd_high)
    {
        *(p_lcd + g_lcd_width*y + x) = color;
    }
}

/*
 * @：画一个矩形
 * x0:代表矩形的起始点X轴坐标
 * y0:代表矩形的起始点y轴坐标
 * w: 屏幕的宽度
 * h: 屏幕的高度
 * color: 矩形填充的颜色
*/
void lcd_draw_rect(int x0, int y0, int w, int h, int color)
{
    if (x0 < 0 || y0 < 0 || w < 0 || h <0)
    {
        printf("draw_rect: param error!\n");
        return;
    }

    if ((x0+w > g_lcd_width) || (y0+h > g_lcd_high))
    {
        printf("draw_rect: param error!\n");
        return;
    }

    // 画一个矩形
    for (int y = y0; y < y0+h; y++) // 行 --> 屏幕的高
    {
        for (int x = x0; x < x0+w; x++) // 列 --> 屏幕的宽
        {
            // 画一个像素点
            lcd_draw_point(x, y, color);
        }
    }
}

/*
 * 加载一张bmp图片
 * bmpfile: 待加载图片的文件名
 * x0: X轴坐标
 * y0：Y轴坐标
*/
void draw_bmp(const char* bmpfile, int x0, int y0)
{
    // 打开bmp文件
    int fd = open(bmpfile, O_RDONLY);
    if (fd == -1)
    {
        perror("open bmpfile error");
        return;
    }

    // 获取文件属性,并偏移bmp图片的文件格式的54个字节
    struct header head;
    struct info in;
    read(fd, &head, sizeof(head));
    read(fd, &in, sizeof(in));

    // 动态开辟内存，用于保存取出出来的像素点
    char* bmpdata = calloc(1, head.size-head.offbytes);

    // 从bmp图片中读取所有的像素点信息
    read(fd, bmpdata, in.width*in.height*in.bit_count/8);

    // 像素点信息已经读取完成，文件就可以关闭了
    close(fd);

    int i = 0;
    for (int y = 0; y < in.height; y++)
    {
        unsigned char r, g, b;
        int color;
        for(int x = 0; x < in.width; x++)
        {
            // bmp图片中像素点读取的顺序是 bgr， lcd屏幕写入时需要按照rgb顺序来写
            b = bmpdata[i++];
            g = bmpdata[i++];
            r = bmpdata[i++];

            // bmp图片一个像素点占3个字节，即 RGB
            // 屏幕一个像素点占4个字节，即 ARGB
            color = (r << 16) | (g << 8) | b;

            lcd_draw_point(x0+x, y0+(in.height-1-y), color);
        }
    }
}

/*
 * 画2048的棋盘矩阵 
*/
void draw_matrix()
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            int x0 = 185, y0 = 25; // 当前矩阵中，第一个矩形的起始点坐标
            if (matrix[i][j] == 0)
			{
                //如果此处元素的值为0，那么就显示
				lcd_draw_rect(x0+j*110,  y0+i*110, 100, 100, 0xb4eeb4);
			}
			else
			{
                // 画对应的图片
				int f_index = get_bmpfiles_index(matrix[i][j]);
				draw_bmp(bmpfiles[f_index], x0+j*110, y0+i*110);
			}
        }
    }
}

/*
	init_matrix:初始化棋盘矩阵
			在任意x个位置，填充x个数字(2,4,8)
*/
void init_matrix(void)
{
	//规则x >= 1,x <= 3
	int x = (random() % 3) + 1;

	int i;

	/*
		step1:随机产生x个数字，并填充到棋盘矩阵中去
	*/
	for(i = 0; i < x; i++)
	{
		int pos = (random() % get_zero_num()) + 1;

		int s[] = {2, 4, 8, 2};
		int s_i = (random() % 3);

		set_matrix(pos, s[s_i]);
	}

	/*
		step 2: 绘制棋盘矩阵
	*/
	draw_matrix();
}

/*
	rand1_matrix:移动之后随机产生一个数字填充到
	任意一个0的位置上
*/
void rand_matrix()
{
	int pos = (random() % get_zero_num()) + 1;

	int s[] = {2, 4, 8, 2};
	int s_i = (random() % 4);

	set_matrix(pos, s[s_i]);
	draw_matrix();
}

/*
	判断是否还能移动
*/
bool is_game_over(void)
{
	int i, j;
	if(get_zero_num() != 0)
	{
		return false;
	}

	for(i = 0; i < BOARDSIZE; i++)
	{
		for(j = 0; j < BOARDSIZE ; j++)
		{
			if (j != BOARDSIZE -1)
			{
				if (matrix[i][j] == matrix[i][j+1])
				{
					return false;
				}
			}

			if (i != BOARDSIZE - 1)
			{
				if (matrix[i][j] == matrix[i+1][j])
				{
					return false;
				}
			}
		}
	}
	
	return true;
}

/*
 * 获取手指在触摸屏上面的滑动方向
 * 返回值：
 *      MOVE_LEFT
 *      MOVE_RIGHT
 *      MOVE_UP
 *      MOVE_DOWN
*/
int get_figer_direction()
{
	// 打开触摸屏对应的模拟驱动文件
    int fd = open("/dev/ubuntu_event", O_RDONLY);
    if (fd == -1)
    {
        perror("open event0 fail");
        return -1;
    }

    struct input_event ev;   // 用于获取触摸屏属性信息
    int x = -1;     		 // 获取x坐标
    int falg_x = false;      // 判断是否获取x坐标
    int y = -1;     		 // 获取y坐标
    int falg_y = false;      // 判断是否获取y坐标
    while(1)
    {
        read(fd, &ev, sizeof(ev));
        if (ev.type == EV_ABS && ev.code == ABS_X) // X轴坐标
        {
			x = ev.value;
			falg_x = true;
        }

        if (ev.type == EV_ABS && ev.code == ABS_Y) // Y轴坐标
        {
			y = ev.value;
			falg_y = true;
        }
		
	//				|
	//				|
	//	左上 --> 上	| 右上 --> 下
	//				|
	//				|
	//	-------------------------
	//				|
	//				|
	//	左下 --> 左	| 右下 --> 右
	//				|
	//				|
        // 将屏幕平均分成4份
		// 左上 --> 上
		// 右上 --> 下
		// 左下 --> 左
		// 右下 --> 右
		int ret = -1;
		if (falg_x && falg_y)
        {
            if (x < 400 && y < 240)
            {
                ret = MOVE_UP;
            }
            else if (x > 400 && y < 240)
            {
                ret = MOVE_DOWN;
            }
            else if (x < 400 && y > 240)
            {
                ret = MOVE_LEFT;
            }
            else if (x > 400 && y > 240)
            {
                ret = MOVE_RIGHT;
            }
			
            close(fd);
			falg_x = false;
			falg_y = false;
			
			return ret;
        }
    }

    close(fd);
}

// 手指左滑
void fin_left()
{
	int i, j;//i为矩阵行下标，j为矩阵列下标
	int value, position;    //value变量用于保存读到的上一个值   position变量用于保存列下标位置
	for(i = 0; i < 4; i++)				 //以行为外层循环
	{
		value = 0;                       //在遍历每行的第一个元素时，value(保存的上一个值) 清0
		position= 0;                     //在遍历每行的第一个元素时，列下标位置清0  
		for(j = 0; j < 4 ; j++)          //以列为内层循环，逐列遍历
		{
			if (matrix[i][j] == 0)
				continue;               //遍历到0，不需要进行任何操作，直接跳过 
									
			if (value == 0)         //遍历到的当前值非0，且value（保存的上一个值）为0
				value = matrix[i][j];     //保存该值到value中
			else                         //value不为0，说明（保存的上一个值）不为0
			{
				if (value == matrix[i][j])        //如果value（上一个值）与当前值相等 
				{
					matrix[i][position++] = value * 2;    // 则进行合并操作，value 乘2  放到矩阵中的该行左侧 
					value = 0;                            // 并且value清0 
				} else {                                  //如果value(上一个值)与当前值不相等 
					matrix[i][position++] = value;       //则将上一个值放到矩阵中的该行左侧
					value = matrix[i][j];                 //并将当前值保存到value中
				}
			}
			matrix[i][j] = 0;                            //移动完成后，当前位置数值清零                           
		}

		if (value != 0)              // 如果保存的值（当前行的最后一个元素）不为0，则将该值放到对应的位置
			matrix[i][position] = value;

	}
		
	bzero(buf_f, sizeof(buf_f));
	sprintf(buf_f, "MOVE_LEFT = %d", MOVE_LEFT);
	display_font_to_screen(buf_f, 20, 0x00FF0000, 620, 100, p_lcd);
	
}

// 手指右滑
void fin_right()
{
	int i, j;             			 //i为矩阵行下标，j为矩阵列下标
	int value;             			 // value变量用于保存读到的上一个值  
	int position;		 		     // position 用于保存列下标位置
	for (i = 0; i < 4; i++)       	 //以行为外层循环
	{
		value = 0;         			 //在遍历每行的第一个元素时，value(保存的上一个值) 清0
		position = 3;  				 //在遍历每行的第一个元素时，列下标位置清0  
		for (j = 3; j >= 0 ; j--)    //以列为内层循环，逐列遍历
		{ 
			if(matrix[i][j] == 0)     //遍历到0，不需要进行任何操作，直接跳过 
			{
				continue;
			}
			if(value == 0)                              //遍历到的当前值非0，且value（保存的上一个值）为0
			{
				value = matrix[i][j];                    //保存该值到value中
			}
			else                                        //value不为0，说明（保存的上一个值）不为0
			{
				if(value == matrix[i][j])                //如果value（上一个值）与当前值相等 
				{
					matrix[i][position--] = 2 * value;   // 则进行合并操作，value 乘2  放到矩阵中的该行右侧
					value = 0;                          // 并且value清0 
				}
				else                                    //如果value(上一个值)与当前值不相等 
				{
					matrix[i][position--] = value;       //则将上一个值放到矩阵中的该行右侧
					value = matrix[i][j];                //并将当前值保存到value中
				}
			}
			matrix[i][j] = 0;                            //移动完成后，当前位置数值清零
		}
		if(value != 0)
		{
			matrix[i][position] = value;                // 如果保存的值（当前行的最后一个元素）不为0，则将该值放到对应的位置
		}
	
	}

	bzero(buf_f, sizeof(buf_f));
	sprintf(buf_f, "MOVE_RIGHT = %d",MOVE_RIGHT);
	display_font_to_screen(buf_f, 20, 0x00FF0000, 620, 100, p_lcd);
}

// 手指上滑
void fin_up()
{
	int i, j;                                     //i为矩阵行下标，j为矩阵列下标           
	int value;                                    //value变量用于保存读到的上一个值  
	int position;                                 //position用于保存行下标位置

	for(j = 0; j < 4; j++)                        //以列为外层循环
	{
		value = 0;                                //在遍历每列的第一个元素时，value(保存的上一个值) 清0
		position= 0;				              //在遍历每列的第一个元素时，行下标位置清0  
		for(i = 0; i < 4 ; i++)                   //以行为内层循环，逐行遍历
		{
		
			if(matrix[i][j] == 0)                  //遍历到0，不需要进行任何操作，直接跳过
			{
				continue;
			}
			if(value == 0)                        //遍历到的当前值非0，且value（保存的上一个值）为0
			{
				value = matrix[i][j];              //保存该值到value中
			}
			else                                  //value不为0，说明（保存的上一个值）不为0
			{
				if(value == matrix[i][j])           //如果value（上一个值）与当前值相等
				{
					matrix[position++][j] =2 * value; //则进行合并操作，value 乘2  放到矩阵中的该列上方
					value = 0;                       // 并且value清0 
				}
				else                                 //如果value(上一个值)与当前值不相等 
				{
					matrix[position++][j] = value;    //则将上一个值放到矩阵中的该列上方
					value = matrix[i][j];             //并将当前值保存到value中
				}
			}
			matrix[i][j] = 0;						 //移动完成后，当前位置数值清零
		}
		if(value != 0)
		{
			matrix[position][j] = value;              //如果保存的值（当前列的最后一个元素）不为0，则将该值放到对应的位置
		}

	}

	bzero(buf_f, sizeof(buf_f));
	sprintf(buf_f, "MOVE_UP = %d", MOVE_UP);
	display_font_to_screen(buf_f, 20, 0x00FF0000, 620, 100, p_lcd);
}

// 手指下滑
void fin_down()
{
	int i, j;                                            //i为矩阵行下标，j为矩阵列下标
	int value;                                       //value变量用于保存读到的上一个值  
	int position;                                    //position用于保存行下标位置
	
	for(j = 0; j < 4; j++)                           //以列为外层循环
	{
		value = 0;                                   //在遍历每列的第一个元素时，value(保存的上一个值) 清0
		position = 3;                                //在遍历每列的第一个元素时，行下标位置清0  
		for(i = 3; i >= 0 ; i--)                     //以行为内层循环，逐行遍历
		{
			if(matrix[i][j] == 0)                     //遍历到0，不需要进行任何操作，直接跳过
			{
				continue;
			}
			if(value == 0)                           //遍历到的当前值非0，且value（保存的上一个值）为0
			{
				value = matrix[i][j];                 //保存该值到value中
			}
			else                                     //value不为0，说明（保存的上一个值）不为0
			{
				if(value == matrix[i][j])             //如果value（上一个值）与当前值相等
				{
					matrix[position--][j] = 2 * value; //则进行合并操作，value 乘2  放到矩阵中的该列下方
					value = 0;                        //并且value清0 
				}
				else                                  //如果value(上一个值)与当前值不相等 
				{
					matrix[position--][j] = value;     //则将上一个值放到矩阵中的该列下方
					value = matrix[i][j];              //并将当前值保存到value中
				}
			}
			matrix[i][j] = 0;                           //移动完成后，当前位置数值清零
		}
		if(value != 0)
		{
			matrix[position][j] = value;              //如果保存的值（当前列的最后一个元素）不为0，则将该值放到对应的位置
		}
		
	}

	bzero(buf_f, sizeof(buf_f));
	sprintf(buf_f, "MOVE_DOWN = %d", MOVE_DOWN);
	display_font_to_screen(buf_f, 20, 0x00FF0000, 620, 100, p_lcd);
}

/*
 * 根据手指滑动来变换棋盘矩阵
*/
void change_matrix()
{
    int dir = get_figer_direction();
    if (dir == MOVE_LEFT)
    {
		//lcd_draw_rect(620, 100, 180, 100, 0xffffff);
		display_font_to_screen(buf_f, 20, 0x00FFFFFF, 620, 100, p_lcd);
        fin_left();
    }
    else if (dir == MOVE_RIGHT)
    {
		//lcd_draw_rect(620, 100, 180, 100, 0xffffff);
		display_font_to_screen(buf_f, 20, 0x00FFFFFF, 620, 100, p_lcd);
        fin_right();
    }
    else if (dir == MOVE_UP)
    {
		//lcd_draw_rect(620, 100, 180, 100, 0xffffff);
		display_font_to_screen(buf_f, 20, 0x00FFFFFF, 620, 100, p_lcd);
        fin_up();
    }
    else if (dir == MOVE_DOWN)
    {
		//lcd_draw_rect(620, 100, 180, 100, 0xffffff);
		display_font_to_screen(buf_f, 20, 0x00FFFFFF, 620, 100, p_lcd);
        fin_down();
    }
}

//显示开机动画
void show_gif()
{
	int i;
	char buf[25] = {0};
	for(i=0; i<35; i++)
	{	
		sprintf(buf, "./gif_bmp/Frame%d.bmp", i);
		printf("%s\n", buf);
		draw_bmp(buf, 0, 0);
		usleep(50*1000);
	}
}

//寻找矩阵元素最大值
void find_max()
{	
	for(int i = 0;i < 4; i++)
	{
		for(int j = 0;j < 4;j++)
		{
			if(matrix[i][j] == 0)
			{
				continue;
			}
			if(matrix[i][j] > max)
			{
				max = matrix[i][j];
			}
		}
	}
	bzero(buf_m,sizeof(buf_m));
	sprintf(buf_m, "当前最大值：%d", max);
	display_font_to_screen(buf_m, 20, 0x00FF0000, 620, 170, p_lcd);
}

//显示步数
void dis_step()
{
	bzero(buf_s,sizeof(buf_s));
	sprintf(buf_s, "当前步数：%d", step);
	display_font_to_screen(buf_s, 20, 0x00FF0000, 620, 140, p_lcd);
}
//显示开机信息
void init_display()
{
	//显示信息
	display_font_to_screen("西安建筑科技大学通信工程02、03班", 20, 0x00FF0000, 250,20, p_lcd);
	display_font_to_screen("组内成员：", 20, 0x00FF0000,40,100, p_lcd);
	display_font_to_screen("张起魁", 20, 0x00FF0000,50 ,135, p_lcd);
	display_font_to_screen("柴永杰", 20, 0x00FF0000,50 ,165, p_lcd);
	display_font_to_screen("陈晟杰", 20, 0x00FF0000,50,195, p_lcd);
	//显示步数
	dis_step();
	//显示最大值
	find_max();

}


int main(int argc,char *argv[])
{
    // 打开屏幕所对应的模拟驱动文件
    int lcd = open("/dev/ubuntu_lcd", O_RDWR);

    // 整个屏幕的大小(单位：byte)
    int screen_size = g_lcd_width * g_lcd_high * g_lcd_bpp/8;

    // 将lcd屏幕的物理内存映射为虚拟内存
    p_lcd = mmap(NULL, screen_size, PROT_WRITE, MAP_SHARED, lcd, 0);

    // 显示开机动画
    show_gif();

    // 清屏
    lcd_draw_rect(0, 0, g_lcd_width, g_lcd_high, 0xffffff);

	srandom(time(NULL)); //设置随机数种子
	init_matrix();  // 初始化棋盘矩阵    

    init_display();

	while (!game_over) //游戏没结束时
	{
		//用来保存原来的矩阵值
		int matrix_v1[BOARDSIZE][BOARDSIZE];
		int i, j, flag = 0;
		for(i = 0; i < BOARDSIZE; ++i)
		{
			for (j = 0; j < BOARDSIZE; ++j)
			{
				matrix_v1[i][j] = matrix[i][j];
			}
		}

		// 变换矩阵
		change_matrix();

        // 矩阵变换后，重新改变数组中的元素值
		for (i = 0; i < BOARDSIZE; ++i)
		{
			for (j = 0; j < BOARDSIZE; ++j)
			{
				if (matrix_v1[i][j] != matrix[i][j])
				{
					flag = 1;
					i = j = BOARDSIZE;
				}
			}
		}
        
        // flag = 1 ： 代表“变换矩阵”后有变化
        // flag = 0 ： 代表“变换矩阵”后没有变化
		if (flag)
		{
            // 空白处产生随机的2、4、8值
			rand_matrix();
            
            // 重新绘制棋盘
			draw_matrix();

			// 记录步数
			step += 1;
		} 
		else 
		{
            // 绘制棋盘
			draw_matrix();
		}
        
		//显示游戏步数
		display_font_to_screen(buf_s, 20, 0x00FFFFFF, 620, 140, p_lcd);
		dis_step();
		//显示当前矩阵中的最大值
		display_font_to_screen(buf_m, 20, 0x00FFFFFF, 620, 170, p_lcd);
		find_max();
        // 判断游戏是否结束
		game_over = is_game_over();
	}

    // 游戏结束后
	lcd_draw_rect(0, 0, g_lcd_width, g_lcd_high, 0xffffff);//清屏
	draw_bmp("res/game_over.bmp", 250, 165);

    // 关闭屏幕的设备文件
    close(lcd);

    // 解除映射
    munmap(p_lcd, screen_size);

    return 0;
}
