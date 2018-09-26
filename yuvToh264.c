#include <stdint.h>
#include <x264.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define CLEAR(x) (memset((&x),0,sizeof(x)))

#define IMAGE_WIDTH   288
#define IMAGE_HEIGHT  352

#define ENCODER_PRESET "veryfast"	//启用各种保护质量的算法
 
#define ENCODER_TUNE   "zerolatency"	//不用缓存,立即返回编码数据
#define ENCODER_PROFILE  "baseline"		//avc 规格,从低到高分别为：Baseline、Main、High。
#define ENCODER_COLORSPACE X264_CSP_I420


typedef struct my_x264_encoder {
	x264_param_t  *x264_parameter;	//x264参数结构体
	x264_t  *x264_encoder;			//控制一帧一帧的编码
	x264_picture_t *yuv420p_picture; //描述视频的特征
	long colorspace;
	unsigned char *yuv;
	x264_nal_t *nal;
    char parameter_preset[20];
    char parameter_tune[20];
    char parameter_profile[20];
} my_x264_encoder;


char *read_file_name = "../xxx.yuv";
char *write_file_name = "test.h264";

int main(int argc,char **argv)
{
	int ret,fd_read,fd_write;
	my_x264_encoder *encoder = (my_x264_encoder*)malloc(sizeof(my_x264_encoder));
	if(encoder == NULL)
	{
		printf("%s\n", "can't malloc my_x264_encoder");
		exit(EXIT_FAILURE);
	}
	CLEAR(*encoder);
 	strcpy(encoder->parameter_preset,ENCODER_PRESET);
    strcpy(encoder->parameter_tune,ENCODER_TUNE);
    encoder->x264_parameter = (x264_param_t*)malloc(sizeof(x264_param_t));
    if(encoder->x264_parameter == NULL)
    {
    	printf("malloc x264_parameter error!\n");
        exit(EXIT_FAILURE);
    }
    CLEAR(*(encoder->x264_parameter));
    x264_param_default(encoder->x264_parameter);	//自动检测系统配置默认参数
    //设置速度和质量要求
    ret = x264_param_default_preset(encoder->x264_parameter,encoder->parameter_preset,encoder->parameter_tune);
    if( ret < 0 )
    {
    	printf("%s\n", "x264_param_default_preset error");
    	exit(EXIT_FAILURE);
    }

    //修改x264的配置参数
    encoder->x264_parameter->i_threads = X264_SYNC_LOOKAHEAD_AUTO;	//cpuFlags 去空缓存继续使用不死锁保证
    encoder->x264_parameter->i_width   = IMAGE_WIDTH;		//宽
    encoder->x264_parameter->i_height  = IMAGE_HEIGHT;		//高
    encoder->x264_parameter->i_frame_total = 0;	//要编码的总帧数,不知道的用0
    encoder->x264_parameter->i_keyint_max  = 25; //设定IDR帧之间的最大间隔
    encoder->x264_parameter->i_bframe 	   = 5;		//两个参考帧之间的B帧数目,该代码可以不设定
    encoder->x264_parameter->b_open_gop	   = 0;		//GOP是指帧间的预测都是在GOP中进行的
    encoder->x264_parameter->i_bframe_pyramid  = 0; //是否允许部分B帧作为参考帧
    encoder->x264_parameter->i_bframe_adaptive = X264_B_ADAPT_TRELLIS; //自适应B帧判定
    encoder->x264_parameter->i_log_level       = X264_LOG_DEBUG;	//日志输出

    encoder->x264_parameter->i_fps_den         = 1;//码率分母
    encoder->x264_parameter->i_fps_num         = 25;//码率分子
    encoder->x264_parameter->b_intra_refresh   = 1;    //是否使用周期帧内涮新替换新的IDR帧    
    encoder->x264_parameter->b_annexb          = 1;    //如果是ture，则nalu 之前的4个字节前缀是0x000001,
                                                            //如果是false,则为大小
    strcpy(encoder->parameter_profile,ENCODER_PROFILE);
    ret = x264_param_apply_profile(encoder->x264_parameter,encoder->parameter_profile); //设置avc 规格
    if( ret < 0 )
    {
    	printf("%s\n", "x264_param_apply_profile error");
    	exit(EXIT_FAILURE);
    }
	//打开编码器
	encoder->x264_encoder = x264_encoder_open(encoder->x264_parameter);
	encoder->colorspace = ENCODER_COLORSPACE;    //设置颜色空间,yuv420的颜色空间
	encoder->yuv420p_picture = (x264_picture_t *)malloc(sizeof(x264_picture_t ));
	if(encoder->yuv420p_picture == NULL)
	{
		printf("%s\n", "encoder->yuv420p_picture malloc error");
		exit(EXIT_FAILURE);
	}
	//按照颜色空间分配内存,返回内存首地址
	ret = x264_picture_alloc(encoder->yuv420p_picture,encoder->colorspace,IMAGE_WIDTH,IMAGE_HEIGHT);
	if( ret<0 )
	{
		printf("%s\n", "x264_picture_alloc malloc error");
		exit(EXIT_FAILURE);
	}
	encoder->yuv420p_picture->img.i_csp = encoder->colorspace;	//配置颜色空间
	encoder->yuv420p_picture->img.i_plane = 3;					//配置图像平面个数
	encoder->yuv420p_picture->i_type = X264_TYPE_AUTO;			//帧的类型,编码过程中自动控制

	encoder->yuv = (uint8_t*)malloc(IMAGE_WIDTH*IMAGE_HEIGHT*3/2);
	if( encoder->yuv == NULL )
	{
		printf("malloc yuv error!\n");
        exit(EXIT_FAILURE);
	}
	CLEAR(*(encoder->yuv));
	encoder->yuv420p_picture->img.plane[0] = encoder->yuv;		//y数据的首地址
	encoder->yuv420p_picture->img.plane[1] = encoder->yuv+IMAGE_WIDTH*IMAGE_HEIGHT;	//u数据的首地址
	encoder->yuv420p_picture->img.plane[2] = encoder->yuv+IMAGE_WIDTH*IMAGE_HEIGHT+IMAGE_WIDTH*IMAGE_HEIGHT/4; //v数据的首地址

   if((fd_read = open(read_file_name,O_RDONLY))<0){
        printf("cannot open input file!\n");
        exit(EXIT_FAILURE);
    }

    if((fd_write = open(write_file_name,O_WRONLY | O_APPEND | O_CREAT,0777))<0){
        printf("cannot open output file!\n");
        exit(EXIT_FAILURE);
    }

	int n_nal = 0;
	x264_picture_t pic_out;
	x264_nal_t *my_nal;
	encoder->nal = (x264_nal_t *)malloc(sizeof(x264_nal_t));

	if(!encoder->nal){
        printf("malloc x264_nal_t error!\n");
        exit(EXIT_FAILURE);
	}
    CLEAR(*(encoder->nal));
    //读取一帧yuv数据
    while(read(fd_read,encoder->yuv,IMAGE_WIDTH*IMAGE_HEIGHT*3/2)>0){
    	 encoder->yuv420p_picture->i_pts++; //一帧的显示时间
    	 //encoder->yuv 的数据在115行处指向了encoder->yuv420p_picture，
    	 //开始转义
    	 if((ret = x264_encoder_encode(encoder->x264_encoder,&encoder->nal,&n_nal,encoder->yuv420p_picture,&pic_out))<0){
            printf("x264_encoder_encode error!\n");
            exit(EXIT_FAILURE);
        }
    	for(my_nal = encoder->nal; my_nal<encoder->nal+n_nal; ++my_nal){
              write(fd_write,my_nal->p_payload,my_nal->i_payload);
        }
        //my_nal->p_payload ： 编码后的数据(int)
        //my_nal->i_payload：编码后的数据大小(uint8*)
       	//可以通过网络程序发送到服务器端保存为文件，后可通过VLC播放
    }

    free(encoder->yuv);
    free(encoder->yuv420p_picture);
    free(encoder->x264_parameter);
    x264_encoder_close(encoder->x264_encoder);
    free(encoder);
    close(fd_read);
    close(fd_write);
    return 0;
}

/**
 * ios 采集数据yuvs数据转换为h264流程
 * 步骤：将采集到的视频数据,分离出Y,U，V,分表与115行处一样,内存首地址相同
 * 之后跟本列程序一样即可，如果需要发送到服务器则应另起线程处理,网络发送另起一个线程
 */























