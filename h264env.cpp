#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <signal.h>

#include <semaphore.h>

#include "MFC_API/SsbSipH264Encode.h"
#include "MFC_API/SsbSipH264Decode.h"
#include "MFC_API/SsbSipMpeg4Decode.h"
#include "MFC_API/SsbSipVC1Decode.h"
#include "FrameExtractor/FrameExtractor.h"
#include "FrameExtractor/MPEG4Frames.h"
#include "FrameExtractor/H263Frames.h"
#include "FrameExtractor/H264Frames.h"
#include "Common/LogMsg.h"
#include "Common/performance.h"
#include "Common/lcd.h"
#include "Common/MfcDriver.h"
#include "FrameExtractor/FileRead.h"
#include "s3c_pp.h"


extern void convert_rgb16_to_yuv420(unsigned char *rgb, unsigned char *yuv, int width, int height);

#define MEDIA_FILE_NAME "/tmp/cam_encoding.264"
#define LCD_BPP_V4L2        V4L2_PIX_FMT_RGB565
#define VIDEO_WIDTH   320
#define VIDEO_HEIGHT  240
#define YUV_FRAME_BUFFER_SIZE   VIDEO_WIDTH*VIDEO_HEIGHT*2
#define PP_DEV_NAME     "/dev/s3c-pp"
#define INPUT_BUFFER_SIZE       (204800)
extern int FriendlyARMWidth, FriendlyARMHeight;
#define FB0_WIDTH FriendlyARMWidth
#define FB0_HEIGHT FriendlyARMHeight
#define FB0_BPP         16
#define FB0_COLOR_SPACE RGB16
static void sig_del_h264(int signo);
static unsigned char delimiter_h264[4]  = {0x00, 0x00, 0x00, 0x01};
#define INPUT_BUFFER_SIZE       (204800)

static void         *handle;
static int          in_fd;
static int          file_size;
static char         *in_addr;
static int          fb_size;
static int          pp_fd, fb_fd;
static char         *fb_addr;

static void sig_del_h264(int signo)
{
    printf("[H.264 display] signal handling\n");

    ioctl(fb_fd, SET_OSD_STOP);
    SsbSipH264DecodeDeInit(handle);

    munmap(in_addr, file_size);
    munmap(fb_addr, fb_size);
    close(pp_fd);
    close(fb_fd);
    close(in_fd);

    exit(1);
}

/******************* CAMERA ********************/
class TError {
public:
    TError(const char *msg) {
        this->msg = msg;
    }
    TError(const TError &e) {
        msg = e.msg;
    }
    void Output() {
        std::cerr << msg << std::endl;
    }
    virtual ~TError() {}
protected:
    TError &operator=(const TError&);
private:
    const char *msg;
};

// Linear memory based image
class TRect {
public:
    TRect():  Addr(0), Size(0), Width(0), Height(0), LineLen(0), BPP(16) {
    }
    virtual ~TRect() {
    }
    bool DrawRect(const TRect &SrcRect, int x, int y) const {
        if (BPP != 16 || SrcRect.BPP != 16) {
            // don't support that yet
            throw TError("does not support other than 16 BPP yet");
        }

        // clip
        int x0, y0, x1, y1;
        x0 = x;
        y0 = y;
        x1 = x0 + SrcRect.Width - 1;
        y1 = y0 + SrcRect.Height - 1;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x0 > Width - 1) {
            return true;
        }
        if( x1 < 0) {
            return true;
        }
        if (x1 > Width - 1) {
            x1 = Width - 1;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (y0 > Height - 1) {
            return true;
        }
        if (y1 < 0) {
            return true;
        }
        if (y1 > Height - 1) {
            y1 = Height -1;
        }

        //copy
        int copyLineLen = (x1 + 1 - x0) * BPP / 8;
        unsigned char *DstPtr = Addr + LineLen * y0 + x0 * BPP / 8;
        const unsigned char *SrcPtr = SrcRect.Addr + SrcRect.LineLen *(y0 - y) + (x0 - x) * SrcRect.BPP / 8;

        for (int i = y0; i <= y1; i++) {
            memcpy(DstPtr, SrcPtr, copyLineLen);
            DstPtr += LineLen;
            SrcPtr += SrcRect.LineLen;
        }


        return true;
    }

    bool DrawRect(const TRect &rect) const { // default is Center
        return DrawRect(rect, /*(Width - rect.Width) / 2*/20, (Height - rect.Height) / 2);
    }

    bool Clear() const {
        int i;
        unsigned char *ptr;
        for (i = 0, ptr = Addr; i < Height; i++, ptr += LineLen) {
            memset(ptr, 0, Width * BPP / 8);
        }
        return true;
    }

    unsigned char *getAddr()
    {
        return Addr;
    }

protected:
    TRect(const TRect&);
    TRect &operator=( const TRect&);

protected:
    unsigned char *Addr;
    int Size;
    int Width, Height, LineLen;
    unsigned BPP;
};



class TFrameBuffer: public TRect {
public:
    TFrameBuffer(const char *DeviceName = "/dev/fb0"): TRect(), fd(-1) {
        Addr = (unsigned char *)MAP_FAILED;

        fd = open(DeviceName, O_RDWR);
        if (fd < 0) {
            throw TError("cannot open frame buffer");
        }

        struct fb_fix_screeninfo Fix;
        struct fb_var_screeninfo Var;
        if (ioctl(fd, FBIOGET_FSCREENINFO, &Fix) < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &Var) < 0) {
            throw TError("cannot get frame buffer information");
        }

        BPP = Var.bits_per_pixel;
            if (BPP != 16) {
            throw TError("support 16BPP frame buffer only");
        }

        //不需要全屏
        Width  = Var.xres;
        Height = Var.yres;
        LineLen = Fix.line_length;
        Size = LineLen * Height;

        int PageSize = getpagesize();
        Size = (Size + PageSize - 1) / PageSize * PageSize ;
            Addr = (unsigned char *)mmap(NULL, Size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, 0);
        if (Addr == (unsigned char *)MAP_FAILED) {
            throw TError("map frame buffer failed");
            return;
        }
        ::close(fd);
        fd = -1;

        /*清零，那我Qt就显示不出来了
        Clear();*/
    }

    virtual ~TFrameBuffer() {
        ::munmap(Addr, Size);
        Addr = (unsigned char *)MAP_FAILED;

        ::close(fd);
        fd = -1;
    }

protected:
    TFrameBuffer(const TFrameBuffer&);
    TFrameBuffer &operator=( const TFrameBuffer&);
private:
    int fd;
};


class TVideo : public TRect {
public:
    TVideo(const char *DeviceName = "/dev/camera"): TRect(), fd(-1) {
        Width = VIDEO_WIDTH;
        Height = VIDEO_HEIGHT;
        BPP = 16;
        LineLen = Width * BPP / 8;
        Size = LineLen * Height;
        Addr = 0;
        fd = ::open(DeviceName, O_RDONLY);
        if (fd < 0) {
            TryAnotherCamera();
        }

        Addr = new unsigned char[Size];
        printf("Addr = %p, Size=%ld\n", Addr, Size);
        //这里可以清零吧？
        Clear();
    }

    bool FetchPicture() const {
        int count = ::read(fd, Addr, Size);
        if (count != Size) {
            throw TError("error in fetching picture from video");
        }
        return true;
    }

    virtual ~TVideo() {
        ::close(fd);
        fd = -1;
        delete[] Addr;
        Addr = 0;
    }

protected:
    TVideo(const TVideo&);
    TVideo &operator=(const TVideo&);

private:
    int fd;
    void TryAnotherCamera();

};


/* MFC functions */
static void *mfc_encoder_init(int width, int height, int frame_rate, int bitrate, int gop_num);
static void *mfc_encoder_exe(void *handle, unsigned char *yuv_buf, int frame_size, int first_frame, long *size);
static void mfc_encoder_free(void *handle);

class TH264Encoder {
public:
    TH264Encoder() {
        frame_count = 0;
        handle = mfc_encoder_init(VIDEO_WIDTH, VIDEO_HEIGHT, 15, 1000, 15);
        if (handle == 0) {
            throw TError("cannot init mfc encoder");
        }
        encoded_fp = fopen(MEDIA_FILE_NAME, "wb+");
        if (encoded_fp == 0) {
            throw TError("cannot open /tmp/cam_encoding.264");
        }
    }

    virtual ~TH264Encoder() {
        mfc_encoder_free(handle);
        fclose(encoded_fp);
    }

    void Encode(TRect &rect)
    {
        frame_count++;
        unsigned char* pRgbData = rect.getAddr();

        convert_rgb16_to_yuv420(pRgbData, g_yuv, VIDEO_WIDTH, VIDEO_HEIGHT);

        //Rgb565ToYuv422(pRgbData, VIDEO_WIDTH, VIDEO_HEIGHT, g_yuv);

        if(frame_count == 1)
            encoded_buf = (unsigned char*)mfc_encoder_exe(handle, g_yuv, YUV_FRAME_BUFFER_SIZE, 1, &encoded_size);
        else
            encoded_buf = (unsigned char*)mfc_encoder_exe(handle, g_yuv, YUV_FRAME_BUFFER_SIZE, 0, &encoded_size);
        fwrite(encoded_buf, 1, encoded_size, encoded_fp);
    }

protected:
    TH264Encoder(const TH264Encoder&);
    TH264Encoder &operator=( const TH264Encoder&);
private:
    int frame_count;
    void* handle;
    FILE* encoded_fp;

    unsigned char   g_yuv[YUV_FRAME_BUFFER_SIZE];
    unsigned char   *encoded_buf;
    long            encoded_size;
};


int playback()
{
    FILE* f = fopen(MEDIA_FILE_NAME,"r");
    if (f == 0) {
        printf("please record first!");
        return -1;
    }
    fclose(f);

    void			*pStrmBuf;
    int				nFrameLeng = 0;
    unsigned int	pYUVBuf[2];

    struct stat				s;
    FRAMEX_CTX				*pFrameExCtx;	// frame extractor context
    FRAMEX_STRM_PTR 		file_strm;
    SSBSIP_H264_STREAM_INFO stream_info;

    s3c_pp_params_t	pp_param;
    s3c_win_info_t	osd_info_to_driver;

    struct fb_fix_screeninfo	lcd_info;

#ifdef FPS
    struct timeval	start, stop;
    unsigned int	time = 0;
    int				frame_cnt = 0;
    int				mod_cnt = 0;
#endif

    if(signal(SIGINT, sig_del_h264) == SIG_ERR) {
        printf("Sinal Error\n");
    }

    // in file open
    in_fd	= open(MEDIA_FILE_NAME, O_RDONLY);
    if(in_fd < 0) {
        printf("Input file open failed\n");
        return -1;
    }

    // get input file size
    fstat(in_fd, &s);
    file_size = s.st_size;

    // mapping input file to memory
    in_addr = (char *)mmap(0, file_size, PROT_READ, MAP_SHARED, in_fd, 0);
    if(in_addr == NULL) {
        printf("input file memory mapping failed\n");
        return -1;
    }

    // Post processor open
    pp_fd = open(PP_DEV_NAME, O_RDWR);
    if(pp_fd < 0)
    {
        printf("Post processor open error\n");
        return -1;
    }

    // LCD frame buffer open
    fb_fd = open("/dev/fb1", O_RDWR|O_NDELAY);
    if(fb_fd < 0)
    {
        printf("LCD frame buffer open error\n");
        return -1;
    }

    ///////////////////////////////////
    // FrameExtractor Initialization //
    ///////////////////////////////////
    pFrameExCtx = FrameExtractorInit(FRAMEX_IN_TYPE_MEM, delimiter_h264, sizeof(delimiter_h264), 1);
    file_strm.p_start = file_strm.p_cur = (unsigned char *)in_addr;
    file_strm.p_end = (unsigned char *)(in_addr + file_size);
    FrameExtractorFirst(pFrameExCtx, &file_strm);


    //////////////////////////////////////
    ///    1. Create new instance      ///
    ///      (SsbSipH264DecodeInit)    ///
    //////////////////////////////////////
    handle = SsbSipH264DecodeInit();
    if (handle == NULL) {
        printf("H264_Dec_Init Failed.\n");
        return -1;
    }

    /////////////////////////////////////////////
    ///    2. Obtaining the Input Buffer      ///
    ///      (SsbSipH264DecodeGetInBuf)       ///
    /////////////////////////////////////////////
    pStrmBuf = SsbSipH264DecodeGetInBuf(handle, nFrameLeng);
    if (pStrmBuf == NULL) {
        printf("SsbSipH264DecodeGetInBuf Failed.\n");
        SsbSipH264DecodeDeInit(handle);
        return -1;
    }

    ////////////////////////////////////
    //  H264 CONFIG stream extraction //
    ////////////////////////////////////
    nFrameLeng = ExtractConfigStreamH264(pFrameExCtx, &file_strm, (unsigned char*)pStrmBuf, INPUT_BUFFER_SIZE, NULL);


    ////////////////////////////////////////////////////////////////
    ///    3. Configuring the instance with the config stream    ///
    ///       (SsbSipH264DecodeExe)                             ///
    ////////////////////////////////////////////////////////////////
    if (SsbSipH264DecodeExe(handle, nFrameLeng) != SSBSIP_H264_DEC_RET_OK) {
        printf("H.264 Decoder Configuration Failed.\n");
        return -1;
    }


    /////////////////////////////////////
    ///   4. Get stream information   ///
    /////////////////////////////////////
    SsbSipH264DecodeGetConfig(handle, H264_DEC_GETCONF_STREAMINFO, &stream_info);

//	printf("\t<STREAMINFO> width=%d   height=%d.\n", stream_info.width, stream_info.height);


    // set post processor configuration
    pp_param.src_full_width	    = stream_info.buf_width;
    pp_param.src_full_height	= stream_info.buf_height;
    pp_param.src_start_x		= 0;
    pp_param.src_start_y		= 0;
    pp_param.src_width			= pp_param.src_full_width;
    pp_param.src_height			= pp_param.src_full_height;
    pp_param.src_color_space	= YC420;
    pp_param.dst_start_x		= 0;
    pp_param.dst_start_y		= 0;
    pp_param.dst_full_width	    = FB0_WIDTH;		// destination width
    pp_param.dst_full_height	= FB0_HEIGHT;		// destination height
    pp_param.dst_width			= pp_param.dst_full_width;
    pp_param.dst_height			= pp_param.dst_full_height;
    pp_param.dst_color_space	= FB0_COLOR_SPACE;
    pp_param.out_path           = DMA_ONESHOT;

    ioctl(pp_fd, S3C_PP_SET_PARAMS, &pp_param);

    // get LCD frame buffer address
    fb_size = pp_param.dst_full_width * pp_param.dst_full_height * 2;	// RGB565
    fb_addr = (char *)mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_addr == NULL) {
        printf("LCD frame buffer mmap failed\n");
        return -1;
    }

    osd_info_to_driver.Bpp			= FB0_BPP;	// RGB16
    osd_info_to_driver.LeftTop_x	= 0;
    osd_info_to_driver.LeftTop_y	= 0;
    osd_info_to_driver.Width		= FB0_WIDTH;	// display width
    osd_info_to_driver.Height		= FB0_HEIGHT;	// display height

    // set OSD's information
    if(ioctl(fb_fd, SET_OSD_INFO, &osd_info_to_driver)) {
        printf("Some problem with the ioctl SET_OSD_INFO\n");
        return -1;
    }

    ioctl(fb_fd, SET_OSD_START);

    while(1)
    {

    #ifdef FPS
        gettimeofday(&start, NULL);
    #endif

        //////////////////////////////////
        ///       5. DECODE            ///
        ///    (SsbSipH264DecodeExe)   ///
        //////////////////////////////////
        if (SsbSipH264DecodeExe(handle, nFrameLeng) != SSBSIP_H264_DEC_RET_OK)
            break;

        //////////////////////////////////////////////
        ///    6. Obtaining the Output Buffer      ///
        ///      (SsbSipH264DecodeGetOutBuf)       ///
        //////////////////////////////////////////////
        SsbSipH264DecodeGetConfig(handle, H264_DEC_GETCONF_PHYADDR_FRAM_BUF, pYUVBuf);


        /////////////////////////////
        // Next H.264 VIDEO stream //
        /////////////////////////////
        nFrameLeng = NextFrameH264(pFrameExCtx, &file_strm, (unsigned char*)pStrmBuf, INPUT_BUFFER_SIZE, NULL);
        if (nFrameLeng < 4)
            break;

        // Post processing
        // pp_param.SrcFrmSt에는 MFC의 output buffer의 physical address가
        // pp_param.DstFrmSt에는 LCD frame buffer의 physical address가 입력으로 넣어야 한다.
        pp_param.src_buf_addr_phy		= pYUVBuf[0];	// MFC output buffer
        ioctl(pp_fd, S3C_PP_SET_SRC_BUF_ADDR_PHY, &pp_param);

        ioctl(fb_fd, FBIOGET_FSCREENINFO, &lcd_info);
        pp_param.dst_buf_addr_phy		= lcd_info.smem_start;			// LCD frame buffer
        ioctl(pp_fd, S3C_PP_SET_DST_BUF_ADDR_PHY, &pp_param);
        ioctl(pp_fd, S3C_PP_START);


    #ifdef FPS
        gettimeofday(&stop, NULL);
        time += measureTime(&start, &stop);
        frame_cnt++;
        mod_cnt++;
        if (mod_cnt == 50) {
            printf("Average FPS : %u\n", (float)mod_cnt*1000/time);
            mod_cnt = 0;
            time = 0;
        }
    #endif


    }

#ifdef FPS
    printf("Display Time : %u, Frame Count : %d, FPS : %f\n", time, frame_cnt, (float)frame_cnt*1000/time);
#endif

    ioctl(fb_fd, SET_OSD_STOP);
    SsbSipH264DecodeDeInit(handle);

    munmap(in_addr, file_size);
    munmap(fb_addr, fb_size);
    close(pp_fd);
    close(fb_fd);
    close(in_fd);

    return 0;
}

static void print_menu(void)
{
    printf("========= S3C6400/6410 Demo Application ==========\n");
    printf("=                                                =\n");
    printf("=  1.   Record                                   =\n");
    printf("=  2.   Playback                                 =\n");
    printf("=  3.   Exit                                     =\n");
    printf("=                                                =\n");
    printf("==================================================\n");
    printf("Select number --> ");
}

/*static void FBOpen();
int main(int argc, char **argv)
{
    int num = 1;
    FBOpen();

    //system("clear");
    //print_menu();
    //scanf("%d", &num);
    //fflush(stdin);
    if (num == 1) {
        try {
            struct timeval start,end;
            TFrameBuffer FrameBuffer;
            TVideo Video;
            int timeuse = 0;
            int oldTimeUse = 0;

            TH264Encoder Encoder;
            gettimeofday( &start, NULL );
            for (;;) {
                Video.FetchPicture();
                //Encoder.Encode(Video);
                FrameBuffer.DrawRect(Video);

                gettimeofday( &end, NULL );
                timeuse = 1000000 * ( end.tv_sec - start.tv_sec ) + end.tv_usec - start.tv_usec;
                timeuse /= 1000000;
                if (oldTimeUse != timeuse) {
                    printf(".\n");
                    oldTimeUse = timeuse;
                }
                if (timeuse > 10) {
                    break;
                }
            }

            printf("\nDone!\n");

        } catch (TError &e) {
            e.Output();
            return 1;
        }
    } else if (num == 2) {
        playback();
    } else {
        exit(0);
    }

    return 0;
}*/

void TVideo::TryAnotherCamera()
{
    int ret, start, found;

    struct v4l2_input chan;
    struct v4l2_framebuffer preview;

    fd = ::open("/dev/video0", O_RDWR);
    if (fd < 0) {
        throw TError("cannot open video device");
    }

    /* Get capability */
    struct v4l2_capability cap;
    ret = ::ioctl(fd , VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        throw TError("not available device");
    }

    /* Check the type - preview(OVERLAY) */
    if (!(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)) {
        throw TError("not available device");
    }
    chan.index = 0;
    found = 0;
    while(1) {
        ret = ::ioctl(fd, VIDIOC_ENUMINPUT, &chan);
        if (ret < 0) {
            throw TError("not available device");
        }

        if (chan.type &V4L2_INPUT_TYPE_CAMERA ) {
            found = 1;
            break;
        }
        chan.index++;
    }

    if (!found) {
        throw TError("not available device");
    }

    chan.type = V4L2_INPUT_TYPE_CAMERA;
    ret = ::ioctl(fd, VIDIOC_S_INPUT, &chan);
    if (ret < 0) {
        throw TError("not available device");
    }

    memset(&preview, 0, sizeof preview);
    preview.fmt.width = Width;
    preview.fmt.height = Height;
    preview.fmt.pixelformat = V4L2_PIX_FMT_RGB565;
    preview.capability = 0;
    preview.flags = 0;

    /* Set up for preview */
    ret = ioctl(fd, VIDIOC_S_FBUF, &preview);
    if (ret< 0) {
        throw TError("not available device");
    }

    /* Preview start */
    start = 1;
    ret = ioctl(fd, VIDIOC_OVERLAY, &start);
    if (ret < 0) {
        throw TError("not available device");
    }
}

/***************** MFC driver function *****************/
void *mfc_encoder_init(int width, int height, int frame_rate, int bitrate, int gop_num)
{
    int             frame_size;
    void            *handle;
    int             ret;


    frame_size  = (width * height * 3) >> 1;

    handle = SsbSipH264EncodeInit(width, height, frame_rate, bitrate, gop_num);
    if (handle == NULL) {
        LOG_MSG(LOG_ERROR, "Test_Encoder", "SsbSipH264EncodeInit Failed\n");
        return NULL;
    }

    ret = SsbSipH264EncodeExe(handle);

    return handle;
}

void *mfc_encoder_exe(void *handle, unsigned char *yuv_buf, int frame_size, int first_frame, long *size)
{
    unsigned char   *p_inbuf, *p_outbuf;
    int             hdr_size;
    int             ret;


    p_inbuf = (unsigned char*)SsbSipH264EncodeGetInBuf(handle, 0);

    memcpy(p_inbuf, yuv_buf, frame_size);

    ret = SsbSipH264EncodeExe(handle);
    if (first_frame) {
        SsbSipH264EncodeGetConfig(handle, H264_ENC_GETCONF_HEADER_SIZE, &hdr_size);
        //printf("Header Size : %d\n", hdr_size);
    }

    p_outbuf = (unsigned char*)SsbSipH264EncodeGetOutBuf(handle, size);

    return p_outbuf;
}

void mfc_encoder_free(void *handle)
{
    SsbSipH264EncodeDeInit(handle);
}


#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

int FriendlyARMWidth, FriendlyARMHeight;
static void FBOpen(void)
{
    struct fb_fix_screeninfo FBFix;
    struct fb_var_screeninfo FBVar;
    int FBHandle = -1;

    FBHandle = open("/dev/fb0", O_RDWR);
    if (ioctl(FBHandle, FBIOGET_FSCREENINFO, &FBFix) == -1 ||
        ioctl(FBHandle, FBIOGET_VSCREENINFO, &FBVar) == -1) {
        fprintf(stderr, "Cannot get Frame Buffer information");
        exit(1);
    }

    FriendlyARMWidth  = FBVar.xres;
    FriendlyARMHeight = FBVar.yres;
    close(FBHandle);
}



