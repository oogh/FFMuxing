//
// Created by Andy on 2019-11-23.
//

#ifndef GHEXPORT_GHEXPORTER_H
#define GHEXPORT_GHEXPORTER_H

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

struct OutputStream {
    AVStream *stream = nullptr;

    AVCodecContext *codecCtx = nullptr;

    AVCodec* codec = nullptr;

    int64_t nextPts;

    int samplesCount;

    AVFrame *dstFrame = nullptr;

    AVFrame *srcFrame = nullptr;

    float t, tincr, tincr2;

    SwsContext *swsCtx = nullptr;

    SwrContext *swrCtx = nullptr;
};

class GHExporter {
public:
    GHExporter();

    ~GHExporter();

    void doExport(const std::string& filename, const int& width, const int& height, const int& fps);

private:
    int open();

    int start();

    int addAudioStream();

    int addVideoStream();

    int openAudioStream();

    int openVideoStream();

    AVFrame* allocAudioFrame(int sampleCount, int sampleRate, uint64_t channelLayout, AVSampleFormat format);

    AVFrame* allocVideoFrame();

    bool writeAudioFrame();

    bool writeVideoFrame();

    int getNextAudioFrame();

    int getNextVideoFrame();

    void fillAudioSamples(AVFrame* frame);

    void fillVideoPixels(AVFrame *frame, int width, int height);

    void closeStream(OutputStream* ost);

    void logPacket(const AVPacket* pkt);
private:
    AVFormatContext* mFormatCtx;

    bool mDisableAudio;
    std::unique_ptr<OutputStream> mAudio;
    bool mDoAudioEncode;

    bool mDisabledVideo;
    std::unique_ptr<OutputStream> mVideo;
    bool mDoVideoEncode;

    int mVideoWrittenCount;

    std::string mFilename;
    int mWidth;
    int mHeight;
    int mFPS;

};



int doExport();

#endif //GHEXPORT_GHEXPORTER_H
