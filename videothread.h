#ifndef VIDEOTHREAD_H
#define VIDEOTHREAD_H

#include <QThread>

class VideoThread : public QThread
{
public:
    VideoThread();
    void stop();
protected:
    void run();
private:
    volatile bool paused;
};

#endif // VIDEOTHREAD_H
