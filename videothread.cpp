#include "videothread.h"
#include "h264env.cpp"

VideoThread::VideoThread()
{
    //初始状态是停止的
    paused = true;
}

void VideoThread::stop()
{
    paused = true;
}

void VideoThread::run()
{
    paused = false;
    FBOpen();
    if (paused == false) {
        try {
            TFrameBuffer FrameBuffer;
            TVideo Video;
            TH264Encoder Encoder;
            for (;;) {
                Video.FetchPicture();
                Encoder.Encode(Video);
                FrameBuffer.DrawRect(Video);

                if (paused == true) {
                    break;
                }
            }
        } catch (TError &e) {
            e.Output();
            //return 1;
        }
    }
}
