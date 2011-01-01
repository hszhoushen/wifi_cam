/*
 *  V4L2 video capture example
 *
 *  This program can be used and distributed without restrictions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */

#include <linux/videodev2.h>

//tcp
#include <arpa/inet.h>
#include <sys/socket.h>

//tcp
void error_handling(char * message);

int tcp_init(int argc, char * argv[]);
int serv_sock;
int clnt_sock;

#define port "10086"

//hszhoushen
int process_mjpg(unsigned char * p);
int send_mjpg(unsigned char *p, size_t len);
int save_mjpg(unsigned char *p, size_t len);

typedef enum {
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
} io_method;

#define CLEAR(x) memset (&(x), 0, sizeof (x))

struct buffer {
        void *                  start;
        size_t                  length;
};

static char *           dev_name        = NULL;
static io_method        io              = IO_METHOD_MMAP;
static int              fd              = -1;
struct buffer *         buffers         = NULL;
static unsigned int     n_buffers       = 0;

static void errno_exit(const char * s)
{
    fprintf (stderr, "%s error %d, %s\n",
             s, errno, strerror (errno));
    exit (EXIT_FAILURE);
}

static int xioctl(int fd,int request,void * arg)
{
    int r;

    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);

    return r;
}

//hszhoushen
//function:remove the redundant 00 and return the size of jpg
int process_mjpg(unsigned char * p)
{
	int len = 0;
	while(p != NULL)
	{
		if((*p) == 255)		//ff
		{
//			printf("%x\n", (*p));
			p++;
			len++;
			if((*p) == 217)	//d9
			{
				printf("%x\n", *p);
				return len+1;
			}
		}
		len++;
		p++;
	}
	return -1;
}
//hszhoushen
//function:send the picture to client
int send_mjpg(unsigned char *p, size_t len)
{
	FILE * fp = NULL;
	fp = fopen("test1.jpg", "w");
	if(fp == NULL)
		return -1;
	
	//如果获取数据异常，退出
	if(len == -1) return -1;
	//获取循环次数，和最后多余数据
	int num = len/4096;
	int yushu = len%4096;
	printf("num = %d, yushu = %d\n", num, yushu);
	//定义循环变量
	size_t j = 0;
	//定义发送字符串
	char send_buf[4096]={0};

	//循环发送数据到client
	for(j = 0; j < num; j++)
	{
		memcpy(send_buf, p+j*4096, 4096);
		
		if(send(clnt_sock, send_buf, 4096, 0) < 0)
		{
			perror("send");
		}

		fwrite(send_buf, sizeof(send_buf), 1, fp);
		memset(send_buf, 0, sizeof(send_buf));
	}

	//剩余部分数据发送到client

	memcpy(send_buf, p+j*4096, yushu);
	fwrite(send_buf, yushu, 1, fp);
	
	if(send(clnt_sock, send_buf, yushu, 0) < 0)
	{
		perror("send");
	}

	sync();
	fclose(fp);
	
	return 0;
}

int save_mjpg(unsigned char *p, size_t len)
{
	FILE * fp = NULL;
	fp = fopen("test.jpg", "w");
	if(fp != NULL)
	{
		fwrite(p, 1, len, fp);
		sync();
		fclose(fp);
	}
	else
	{
		return -1;
	}
	return 0;
}

//从内核缓冲区取出一帧数据
static int read_frame(void)
{
    struct v4l2_buffer buf;
    unsigned int i;
	char send_buf[4096] = {0};
	char temp_buf[50] = {0};
	
	size_t len = 0;

    CLEAR (buf);

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
	/*从内核环形缓冲区取出一帧数据*/
    if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) 
	{
        switch (errno) 
		{
            case EAGAIN:
                    return 0;

            case EIO:
                    /* Could ignore EIO, see spec. */

                    /* fall through */

            default:
                    errno_exit ("VIDIOC_DQBUF");
        }
    }
	
    assert (buf.index < n_buffers);
	
	//move the redundant 00,and get the size of jpg
	len = process_mjpg(buffers[buf.index].start);
	if(len != -1){
		buffers[buf.index].length = len;	
		printf("len = %d\n", buffers[buf.index].length);		
	}

	//send_mjpg to the client
	send_mjpg(buffers[buf.index].start, len);
	
	//保存为图片  test.jpg
	save_mjpg(buffers[buf.index].start, len);

    if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
            errno_exit ("VIDIOC_QBUF");
      
    return 1;
}

static void mainloop(void)
{
    unsigned int count;

    count = 1;

    while (count-- > 0) 
	{
        for (;;) 
		{
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO (&fds);
            FD_SET (fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select (fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r) 
			{
                    if (EINTR == errno)
                            continue;
                    errno_exit ("select");
            }

            if (0 == r) {
                    fprintf (stderr, "select timeout\n");
                    exit (EXIT_FAILURE);
            }

            if (read_frame())
                break;
            /* EAGAIN - continue select loop. */
        }
    }
}

static void stop_capturing(void)
{
    enum v4l2_buf_type type;

    switch (io) 
	{
	    case IO_METHOD_READ:
	            /* Nothing to do. */
	            break;

	    case IO_METHOD_MMAP:
	    case IO_METHOD_USERPTR:
	            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	            if (-1 == xioctl (fd, VIDIOC_STREAMOFF, &type))
	                   errno_exit ("VIDIOC_STREAMOFF");
	            break;
    }
}

static void start_capturing(void)
{
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < n_buffers; ++i) 
	{
        struct v4l2_buffer buf;

        CLEAR (buf);

		/*1.将内存放入缓冲区*/
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;

        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
                errno_exit ("VIDIOC_QBUF");
    }
    
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/*2.开始传输采集图像*/
    if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
            errno_exit ("VIDIOC_STREAMON");
	  
}

static void uninit_device(void)
{
    unsigned int i;

	for (i = 0; i < n_buffers; ++i)
	        if (-1 == munmap (buffers[i].start, buffers[i].length))
	                errno_exit ("munmap");
    free (buffers);
}

static void init_read(unsigned int buffer_size)
{
        buffers = calloc (1, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc (buffer_size);

        if (!buffers[0].start) 
		{
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }
}

static void init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR (req);

    req.count        = 4;
    req.type         = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory       = V4L2_MEMORY_MMAP;

    if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) 
	{
        if (EINVAL == errno) {
                fprintf (stderr, "%s does not support "
                         "memory mapping\n", dev_name);
                exit (EXIT_FAILURE);
        } 
		else {
                errno_exit ("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
            fprintf (stderr, "Insufficient buffer memory on %s\n",
                     dev_name);
            exit (EXIT_FAILURE);
    }

    buffers = calloc (req.count, sizeof (*buffers));

    if (!buffers) {
            fprintf (stderr, "Out of memory\n");
            exit (EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
            struct v4l2_buffer buf;

            CLEAR (buf);

            buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory      = V4L2_MEMORY_MMAP;
            buf.index       = n_buffers;

            if (-1 == xioctl (fd, VIDIOC_QUERYBUF, &buf))
                    errno_exit ("VIDIOC_QUERYBUF");

            buffers[n_buffers].length = buf.length;
            buffers[n_buffers].start =
                    mmap (NULL /* start anywhere */,
                          buf.length,
                          PROT_READ | PROT_WRITE /* required */,
                          MAP_SHARED /* recommended */,
                          fd, buf.m.offset);

            if (MAP_FAILED == buffers[n_buffers].start)
                    errno_exit ("mmap");
    }
}

static void init_userp(unsigned int buffer_size)
{
        struct v4l2_requestbuffers req;
        unsigned int page_size;

        page_size = getpagesize ();
        buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

        CLEAR (req);

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
                        errno_exit ("VIDIOC_REQBUFS");
                }
        }

        buffers = calloc (4, sizeof (*buffers));

        if (!buffers) {
                fprintf (stderr, "Out of memory\n");
                exit (EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = memalign (/* boundary */ page_size,
                                                     buffer_size);

                if (!buffers[n_buffers].start) {
                        fprintf (stderr, "Out of memory\n");
                        exit (EXIT_FAILURE);
                }
        }
}

static void init_device(void)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        unsigned int min;

//2016.1.10 hszhoushen
		int ret = 0;
		struct v4l2_fmtdesc fmt1;
		int len = 0;
		/*1.查询设备属性*/
        if (-1 == xioctl (fd, VIDIOC_QUERYCAP, &cap)) 
		{
                if (EINVAL == errno) 
				{
                        fprintf (stderr, "%s is no V4L2 device\n",
                                 dev_name);
                        exit (EXIT_FAILURE);
                } 
				
				else 
				{
                        errno_exit ("VIDIOC_QUERYCAP");
                }
        }
		/*2.是否支持图像获取*/
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf (stderr, "%s is no video capture device\n",
                         dev_name);
                exit (EXIT_FAILURE);
        }
		
//2015.12.12_17:18_zhou_add
		/*3.查询设备是否支持流输出*/
	    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
	        fprintf(stderr, "%s does not support streaming i/o\n",dev_name);
	        exit(EXIT_FAILURE);
	    }
//2016.1.10_17:18_zhou_add
#if 0
		memset(&fmt1, 0, sizeof(fmt1));
		fmt1.index = 0;
		fmt1.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		printf("camera function\n");
		while ((ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmt1)) == 0) 
		{
			printf("{ pixelformat = '%c%c%c%c', description = '%s' }\n",
			fmt1.pixelformat & 0xFF, (fmt1.pixelformat >> 8) & 0xFF,
			(fmt1.pixelformat >> 16) & 0xFF, (fmt1.pixelformat >> 24) & 0xFF,
			fmt1.description);   	
		}
		printf("function end\n");
#endif
		printf("VIDOOC_QUERYCAP\n");
    	printf("driver:\t\t%s\n",cap.driver);
    	printf("card:\t\t%s\n",cap.card);
   	 	printf("bus_info:\t%s\n",cap.bus_info);
    	printf("version:\t%d\n",cap.version);
    	printf("capabilities:\t%x\n",cap.capabilities);
		
        switch (io) {
	        case IO_METHOD_READ:
	                if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
	                        fprintf (stderr, "%s does not support read i/o\n",
	                                 dev_name);
	                        exit (EXIT_FAILURE);
	                }

	                break;

	        case IO_METHOD_MMAP:
	        case IO_METHOD_USERPTR:
	                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
	                        fprintf (stderr, "%s does not support streaming i/o\n",
	                                 dev_name);
	                        exit (EXIT_FAILURE);
	                }

	                break;
        }


        /* Select video input, video standard and tune here. */
		/* set video param */


        CLEAR (cropcap);

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl (fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl (fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } 
		else {        
                /* Errors ignored. */
        }


        CLEAR (fmt);

        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = 640; 
        fmt.fmt.pix.height      = 480;
//        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		
        fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

        if (-1 == xioctl (fd, VIDIOC_S_FMT, &fmt))
                errno_exit ("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */

        /* Buggy driver paranoia. */
        min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min)
                fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min)
                fmt.fmt.pix.sizeimage = min;

        init_mmap ();
}

static void close_device(void)
{
    if (-1 == close (fd))
            errno_exit ("close");

    fd = -1;
}

static void  open_device(void)
{
	struct stat st; 

	if (-1 == stat (dev_name, &st)) 
	{
	        fprintf (stderr, "Cannot identify '%s': %d, %s\n",
	                 dev_name, errno, strerror (errno));
	        exit (EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) 
	{
	        fprintf (stderr, "%s is no device\n", dev_name);
	        exit (EXIT_FAILURE);
	}

	//非阻塞模式，应用程序能够使用阻塞模式或非阻塞模式打开视频设备，
	//如果使用非阻塞模式调用视频设备，即使尚未捕获到信息，驱动依旧会把缓存（DQBUFF）里的东西返回给应用程序。
	fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);


	if (-1 == fd) 
	{
	        fprintf (stderr, "Cannot open '%s': %d, %s\n",
	                 dev_name, errno, strerror (errno));
	        exit (EXIT_FAILURE);
	}
		
}

static void usage(FILE * fp,int argc,char ** argv)
{
        fprintf (fp,
                 "Usage: %s [options]\n\n"
                 "Options:\n"
                 "-d | --device name   Video device name [/dev/video]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "",
                 argv[0]);
}

static const char short_options [] = "d:hmru";

static const struct option
long_options [] = {
        { "device",     required_argument,      NULL,           'd' },
        { "help",       no_argument,            NULL,           'h' },
        { "mmap",       no_argument,            NULL,           'm' },
        { "read",       no_argument,            NULL,           'r' },
        { "userp",      no_argument,            NULL,           'u' },
        { 0, 0, 0, 0 }
};

int main(int  argc,  char * argv[])
{
	dev_name = "/dev/video0";

	printf("open device\n\r");
	open_device ();	

	printf("Init device\r\n");
	init_device ();

	printf("start_capture\n");
	start_capturing ();

	printf("tcp init\n\r");
	tcp_init(argc, argv);
	
	printf("main_loop\n");
	mainloop ();

	printf("stop capturing\n");
	stop_capturing ();

	printf("uninit device\n");
	uninit_device ();

	printf("close device\n");
	close_device ();

	close(clnt_sock);
	close(serv_sock);
	exit (EXIT_SUCCESS);

	return 0;
}


int tcp_init(int argc, char * argv[])
{

	struct sockaddr_in serv_addr;
	struct sockaddr_in clnt_addr;

	socklen_t clnt_addr_size;

	char message[] = "hello world!";
	char ip[20];

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock == -1)
		error_handling("socket() error");

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(port));

	if(bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("bind() error");

	if(listen(serv_sock, 5) == -1)
		error_handling("listen() error");

//	while(1)
//	{
		memset(&clnt_addr, 0, sizeof(clnt_addr));
		clnt_addr_size = sizeof(struct sockaddr_in);

		//等待接受新的连接到来，并返回新连接的套接字描述符，accept默认为阻塞函数
		clnt_sock = accept(serv_sock,(struct sockaddr *)&clnt_addr, &clnt_addr_size);
		if(clnt_sock < 0){
			perror("accept");
			exit(1);
		}
		
		//将新的连接的二进制地址转换成点分十进制，和端口号一起打印
		inet_ntop(AF_INET, (void *)&clnt_addr.sin_addr, ip, clnt_addr_size);
		printf("Remote request the current time :%s(%d)\n",ip, ntohs(clnt_addr.sin_port));

		//向新连接发送时间t,send返回发送的数据大小，失败返回-1
		if(send(clnt_sock, message, sizeof(message), 0))
		{
			perror("send");
		}
		//关闭描述符，与新连接的通信结束
//		close(clnt_sock);
//	}


//	write(clnt_sock, message, sizeof(message));
//	close(serv_sock);

	return 0;		
}

void error_handling(char * message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}



