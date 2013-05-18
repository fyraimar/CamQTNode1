#-------------------------------------------------
#
# Project created by QtCreator 2013-04-25T00:16:16
#
#-------------------------------------------------

QT       += core gui

TARGET = CNode_1
TEMPLATE = app


SOURCES += main.cpp\
        widget.cpp \
    h264env.cpp \
    Common/performance.c \
    Common/LogMsg.c \
    FrameExtractor/VC1Frames.c \
    FrameExtractor/MPEG4Frames.c \
    FrameExtractor/H264Frames.c \
    FrameExtractor/H263Frames.c \
    FrameExtractor/FrameExtractor.c \
    FrameExtractor/FileRead.c \
    MFC_API/SsbSipVC1Decode.c \
    MFC_API/SsbSipMpeg4Encode.c \
    MFC_API/SsbSipMpeg4Decode.c \
    MFC_API/SsbSipMfcDecode.c \
    MFC_API/SsbSipH264Encode.c \
    MFC_API/SsbSipH264Decode.c \
    videothread.cpp \
    convert_rgb16_to_yuv420.cpp

HEADERS  += widget.h \
    Common/videodev2_s3c.h \
    Common/videodev2.h \
    Common/post.h \
    Common/performance.h \
    Common/MfcDrvParams.h \
    Common/MfcDriver.h \
    Common/mfc.h \
    Common/LogMsg.h \
    Common/lcd.h \
    FrameExtractor/VC1Frames.h \
    FrameExtractor/MPEG4Frames.h \
    FrameExtractor/H264Frames.h \
    FrameExtractor/H263Frames.h \
    FrameExtractor/FrameExtractor.h \
    FrameExtractor/FileRead.h \
    MFC_API/SsbSipVC1Decode.h \
    MFC_API/SsbSipMpeg4Encode.h \
    MFC_API/SsbSipMpeg4Decode.h \
    MFC_API/SsbSipMfcDecode.h \
    MFC_API/SsbSipH264Encode.h \
    MFC_API/SsbSipH264Decode.h \
    videothread.h

FORMS    += widget.ui

OTHER_FILES += \
    Common/performance.o \
    Common/Makefile \
    Common/LogMsg.o \
    FrameExtractor/VC1Frames.o \
    FrameExtractor/MPEG4Frames.o \
    FrameExtractor/Makefile \
    FrameExtractor/H264Frames.o \
    FrameExtractor/H263Frames.o \
    FrameExtractor/FrameExtractor.o \
    FrameExtractor/FileRead.o \
    MFC_API/SsbSipVC1Decode.o \
    MFC_API/SsbSipMpeg4Encode.o \
    MFC_API/SsbSipMpeg4Decode.o \
    MFC_API/SsbSipMfcDecode.o \
    MFC_API/SsbSipH264Encode.o \
    MFC_API/SsbSipH264Decode.o \
    MFC_API/Makefile
