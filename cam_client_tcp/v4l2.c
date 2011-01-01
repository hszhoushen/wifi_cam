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
int socket = 0;

#define port "10086"




int process_mjpg(unsigned char * p);

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

static void process_image(const void * p)
{
        fputc ('.', stdout);
        fflush (stdout);
}


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
//从内核缓冲区取出一帧数据
static int read_frame(void)
{
        struct v4l2_buffer buf;
        unsigned int i;
		
		size_t len = 0;

        switch (io) 
		{
	        case IO_METHOD_READ:
	                if (-1 == read (fd, buffers[0].start, buffers[0].length)) {
	                        switch (errno) {
	                        case EAGAIN:
	                                return 0;

	                        case EIO:
	                                /* Could ignore EIO, see spec. */

	                                /* fall through */

	                        default:
	                                errno_exit ("read");
	                        }
	                }

	                process_image (buffers[0].start);

					
	                break;

	        case IO_METHOD_MMAP:
	                CLEAR (buf);

	                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	                buf.memory = V4L2_MEMORY_MMAP;
					/*从内核环形缓冲区取出一帧数据*/
	                if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
	                        switch (errno) {
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

	                process_image (buffers[buf.index].start);

//2015.12.12_18:33:zhou_add
#if 1	
					len = process_mjpg(buffers[buf.index].start);
					if(len != -1){
						buffers[buf.index].length = len;

//						buffers[buf.index].length = 6555500;  it's ok
							
						printf("len = %d\n", buffers[buf.index].length);
						
					}
					
					//保存为图片  test.jpg
					FILE * fp = NULL;
					fp = fopen("test.jpg", "w");
					if(fp != NULL)
					{
						fwrite(buffers[buf.index].start, 1,buffers[buf.index].length, fp);
						sync();
						fclose(fp);
					}
#endif
//end

	                if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	                        errno_exit ("VIDIOC_QBUF");

	                break;

	        case IO_METHOD_USERPTR:
	                CLEAR (buf);

	                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	                buf.memory = V4L2_MEMORY_USERPTR;

	                if (-1 == xioctl (fd, VIDIOC_DQBUF, &buf)) {
	                        switch (errno) {
	                        case EAGAIN:
	                                return 0;

	                        case EIO:
	                                /* Could ignore EIO, see spec. */

	                                /* fall through */

	                        default:
	                                errno_exit ("VIDIOC_DQBUF");
	                        }
	                }

	                for (i = 0; i < n_buffers; ++i)
	                        if (buf.m.userptr == (unsigned long) buffers[i].start
	                            && buf.length == buffers[i].length)
	                                break;

	                assert (i < n_buffers);

	                process_image ((void *) buf.m.userptr);

	                if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	                        errno_exit ("VIDIOC_QBUF");

	                break;
	        }
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

                        if (read_frame ())
                                break;
        
                        /* EAGAIN - continue select loop. */
                }
        }
}

static void
stop_capturing                  (void)
{
        enum v4l2_buf_type type;

        switch (io) {
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

        switch (io) 
		{
	        case IO_METHOD_READ:
	                /* Nothing to do. */
	                break;

	        case IO_METHOD_MMAP:
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

	                break;

	        case IO_METHOD_USERPTR:
	                for (i = 0; i < n_buffers; ++i) 
					{
	                        struct v4l2_buffer buf;

	                        CLEAR (buf);

	                        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	                        buf.memory      = V4L2_MEMORY_USERPTR;
	                        buf.index       = i;
	                        buf.m.userptr   = (unsigned long) buffers[i].start;
	                        buf.length      = buffers[i].length;

	                        if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
	                                errno_exit ("VIDIOC_QBUF");
	                }

	                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	                if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
	                        errno_exit ("VIDIOC_STREAMON");

	                break;
	        }
}

static void uninit_device(void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free (buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap (buffers[i].start, buffers[i].length))
                                errno_exit ("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free (buffers[i].start);
                break;
        }

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

        req.count               = 4;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_MMAP;

        if (-1 == xioctl (fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf (stderr, "%s does not support "
                                 "memory mapping\n", dev_name);
                        exit (EXIT_FAILURE);
                } else {
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
        } else {        
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

        switch (io) {
        case IO_METHOD_READ:
                init_read (fmt.fmt.pix.sizeimage);
                break;

        case IO_METHOD_MMAP:
                init_mmap ();
                break;

        case IO_METHOD_USERPTR:
                init_userp (fmt.fmt.pix.sizeimage);
                break;
        }
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

        if (-1 == stat (dev_name, &st)) {
                fprintf (stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror (errno));
                exit (EXIT_FAILURE);
        }

        if (!S_ISCHR (st.st_mode)) {
                fprintf (stderr, "%s is no device\n", dev_name);
                exit (EXIT_FAILURE);
        }

		//非阻塞模式，应用程序能够使用阻塞模式或非阻塞模式打开视频设备，
		//如果使用非阻塞模式调用视频设备，即使尚未捕获到信息，驱动依旧会把缓存（DQBUFF）里的东西返回给应用程序。
        fd = open (dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);


        if (-1 == fd) {
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

		printf("tcp init\n\r");

		tcp_init(argc, argv);

		printf("open device\n\r");
        open_device ();	
		
		printf("Init device\r\n");
        init_device ();

		printf("start_capture\n");
        start_capturing ();

		printf("main_loop\n");
        mainloop ();

		printf("stop capturing\n");
        stop_capturing ();
		
		printf("uninit device\n");
        uninit_device ();

		printf("close device\n");
        close_device ();

		close(socket);
        exit (EXIT_SUCCESS);

        return 0;
}


int tcp_init(int argc, char * argv[])
{

	struct sockaddr_in  serv_addr;
	char message[30];
	int str_len;

	if(argc != 2)
	{
		printf("Usage : %s <IP> \n", argv[0]);
		exit(1);
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock == -1)
		error_handling("socket() error");

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family  = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	serv_addr.sin_port = htons(atoi(port));

	if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("connect() error");

	str_len = read(sock, message, sizeof(message) - 1);
	if(str_len == -1)
		error_handling("read() error!");

	printf("Message from server : %s \n", message);


	return 0;
}

void error_handling(char * message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}



