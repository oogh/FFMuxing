//
// Created by Andy on 2019-11-23.
//

#include "GHExporter.h"

#define STREAM_DURATION 10.0
#define STREAM_FRAME_RATE 25
#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P

#define SCALE_FLAGS SWS_BICUBIC

GHExporter::GHExporter() : mFormatCtx(nullptr),
                           mDisableAudio(false),
                           mDisabledVideo(false),
                           mDoAudioEncode(false),
                           mDoVideoEncode(false),
                           mVideoWrittenCount(0) {

}

GHExporter::~GHExporter() {

}

void GHExporter::doExport(const std::string &filename, const int &width, const int &height, const int &fps) {
    if (filename.empty() || width <= 0 || height <= 0) {
        av_log(nullptr, AV_LOG_FATAL, "doExport() failed!!! error: Invalid Argument\n");
        return;
    }

    mFilename = filename;
    mWidth = width;
    mHeight = height;
    mFPS = fps;

    if (open() < 0) {
        goto EXPORT_OUT;
    }

    if (start() < 0) {
        goto EXPORT_OUT;
    }

    EXPORT_OUT:
    if (mFormatCtx) {
        if (mAudio) {
            closeStream(mAudio.get());
            mAudio.reset();
        }

        if (mVideo) {
            closeStream(mVideo.get());
            mVideo.reset();
        }

        if (!(mFormatCtx->flags & AVFMT_NOFILE)) {
            avio_closep(&mFormatCtx->pb);
        }

        avformat_close_input(&mFormatCtx);
        av_log(nullptr, AV_LOG_FATAL, "[Exporter] ended\n");
    }
}

int GHExporter::open() {
    int ret;
    AVOutputFormat *oformat = nullptr;

    ret = avformat_alloc_output_context2(&mFormatCtx, nullptr, nullptr, mFilename.data());
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avformat_alloc_output_context2() error: %s", av_err2str(ret));
        return ret;
    }

    oformat = mFormatCtx->oformat;

    // 使用内部复用器内部编码器
    if (oformat->audio_codec != AV_CODEC_ID_NONE) {
        if (!mDisableAudio) {
            if (addAudioStream() < 0 && openAudioStream() < 0) {
                return -1;
            } else {
                if (openAudioStream() < 0) {
                    return -1;
                } else {
                    mDoAudioEncode = true;
                }
            }
        }
    }

    if (oformat->video_codec != AV_CODEC_ID_NONE) {
        if (!mDisabledVideo) {
            if (addVideoStream() < 0) {
                return -1;
            } else {
                if (openVideoStream() < 0) {
                    return -1;
                } else {
                    mDoVideoEncode = true;
                }
            }
        }
    }

    if (!(mFormatCtx->flags & AVFMT_NOFILE)) {
        if (avio_open(&mFormatCtx->pb, mFilename.data(), AVIO_FLAG_WRITE) < 0) {
            return -1;
        }
    }

    return 0;
}

int GHExporter::start() {

    av_log(nullptr, AV_LOG_FATAL, "[Exporter] begin\n");

    if (!mFormatCtx) {
        av_log(nullptr, AV_LOG_FATAL, "start() error: cannot open output file!\n");
        return -1;
    }

    int ret = avformat_write_header(mFormatCtx, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avformat_write_header() error: %s!", av_err2str(ret));
        return -1;
    }

    // 编码音频/视频，当视频nextPts > 音频nextPts时，先编码音频帧
    while (mDoAudioEncode || mDoVideoEncode) {
        if (mDoAudioEncode &&
            (!mDoVideoEncode || av_compare_ts(mVideo->nextPts, mVideo->codecCtx->time_base,
                                              mAudio->nextPts, mAudio->codecCtx->time_base) > 0)) {
            mDoAudioEncode = writeAudioFrame();
        } else {
            mDoVideoEncode = writeVideoFrame();
        }
    }

    ret = av_write_trailer(mFormatCtx);
    if (ret != 0) {
        av_log(nullptr, AV_LOG_FATAL, "av_write_trailer() error: %s!", av_err2str(ret));
        return -1;
    }

    return 0;
}

int GHExporter::addAudioStream() {

    AVCodec *codec = avcodec_find_encoder(mFormatCtx->oformat->audio_codec);
    if (!codec) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_find_encoder() error: cannot find audio encoder %s",
               avcodec_get_name(mFormatCtx->oformat->audio_codec));
        return -1;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_alloc_context3() failed!!!\n");
        return -1;
    }

    codecCtx->sample_fmt = codec->sample_fmts ?
                           codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    codecCtx->bit_rate = 64000;
    codecCtx->sample_rate = 44100;
    if (codec->supported_samplerates) {
        codecCtx->sample_rate = codec->supported_samplerates[0];
        for (int i = 0; codec->supported_samplerates[i]; i++) {
            if (codec->supported_samplerates[i] == 44100)
                codecCtx->sample_rate = 44100;
        }
    }
    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);
    codecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
    if (codec->channel_layouts) {
        codecCtx->channel_layout = codec->channel_layouts[0];
        for (int i = 0; codec->channel_layouts[i]; i++) {
            if (codec->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                codecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        }
    }
    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);

    // 这里的codec传入null，使用自己创建的codecCtx。否则的话，FFmpeg内部会自己创建codecCtx，不方便我们参数的设置
    AVStream *stream = avformat_new_stream(mFormatCtx, nullptr);
    if (!stream) {
        av_log(nullptr, AV_LOG_FATAL, "avformat_new_stream() failed!!!\n");
        return -1;
    }

    mAudio = std::make_unique<OutputStream>();
    mAudio->stream = stream;
    mAudio->codecCtx = codecCtx;
    mAudio->codec = codec;


    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_log(nullptr, AV_LOG_FATAL, "[Exporter] add audio stream\n");

    return 0;
}

int GHExporter::addVideoStream() {

    AVCodec *codec = avcodec_find_encoder(mFormatCtx->oformat->video_codec);
    if (!codec) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_find_encoder() error: cannot find video encoder %s",
               avcodec_get_name(mFormatCtx->oformat->video_codec));
        return -1;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_alloc_context3() failed!!!\n");
        return -1;
    }

    codecCtx->codec_id = mFormatCtx->oformat->video_codec;
    codecCtx->bit_rate = 400000;
    codecCtx->width = mWidth;
    codecCtx->height = mHeight;
    codecCtx->time_base = {1, STREAM_FRAME_RATE};
    codecCtx->gop_size = 12;
    codecCtx->pix_fmt = STREAM_PIX_FMT;

    // 这里的codec传入null，使用自己创建的codecCtx。否则的话，FFmpeg内部会自己创建codecCtx，不方便我们参数的设置
    AVStream *stream = avformat_new_stream(mFormatCtx, nullptr);
    if (!stream) {
        av_log(nullptr, AV_LOG_FATAL, "avformat_new_stream() failed!!!\n");
        return -1;
    }

    mVideo = std::make_unique<OutputStream>();
    mVideo->stream = stream;
    mVideo->codecCtx = codecCtx;
    mVideo->codec = codec;


    if (mFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_log(nullptr, AV_LOG_FATAL, "[Exporter] add video stream\n");

    return 0;
}

int GHExporter::openAudioStream() {
    if (!mAudio) {
        av_log(nullptr, AV_LOG_FATAL, "openAudioStream() error: cannot find audio stream!\n");
        return -1;
    }

    int ret;
    int sampleCount = 0;
    AVCodecContext *codecCtx = mAudio->codecCtx;
    AVCodec *codec = mAudio->codec;

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_open2() error: %s\n", av_err2str(ret));
        return -1;
    }

    ret = avcodec_parameters_from_context(mAudio->stream->codecpar, codecCtx);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_parameters_from_context() error: %s\n", av_err2str(ret));
        return -1;
    }

    if (codecCtx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        sampleCount = 10000;
    else
        sampleCount = codecCtx->frame_size;

    mAudio->srcFrame = allocAudioFrame(sampleCount, codecCtx->sample_rate, codecCtx->channel_layout, AV_SAMPLE_FMT_S16);
    if (!mAudio->srcFrame) {
        av_log(nullptr, AV_LOG_FATAL, "src allocAudioFrame() failed!!!\n");
        return -1;
    }

    mAudio->dstFrame = allocAudioFrame(sampleCount, codecCtx->sample_rate, codecCtx->channel_layout,
                                       codecCtx->sample_fmt);
    if (!mAudio->dstFrame) {
        av_log(nullptr, AV_LOG_FATAL, "dst allocAudioFrame() failed!!!\n");
        return -1;
    }

    mAudio->swrCtx = swr_alloc();
    if (!mAudio->swrCtx) {
        av_log(nullptr, AV_LOG_FATAL, "swr_alloc() failed!!!\n");
        return -1;
    }

    av_opt_set_int(mAudio->swrCtx, "in_channel_count", codecCtx->channels, 0);
    av_opt_set_int(mAudio->swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(mAudio->swrCtx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(mAudio->swrCtx, "out_channel_count", codecCtx->channels, 0);
    av_opt_set_int(mAudio->swrCtx, "out_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(mAudio->swrCtx, "out_sample_fmt", codecCtx->sample_fmt, 0);

    if (swr_init(mAudio->swrCtx) < 0) {
        av_log(nullptr, AV_LOG_FATAL, "swr_init() failed!!!\n");
        return -1;
    }

    mAudio->t = 0;
    mAudio->tincr = 2 * M_PI * 110.0 / codecCtx->sample_rate;
    mAudio->tincr2 = 2 * M_PI * 110.0 / codecCtx->sample_rate / codecCtx->sample_rate;

    av_log(nullptr, AV_LOG_FATAL, "[Exporter] open audio stream\n");

    return 0;
}

int GHExporter::openVideoStream() {

    if (!mVideo) {
        av_log(nullptr, AV_LOG_FATAL, "openVideoStream() error: cannot find video stream!\n");
        return -1;
    }

    int ret;
    AVCodecContext *codecCtx = mVideo->codecCtx;
    AVCodec *codec = mVideo->codec;

    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_open2() error: %s\n", av_err2str(ret));
        return -1;
    }

    ret = avcodec_parameters_from_context(mVideo->stream->codecpar, codecCtx);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_parameters_from_context() error: %s\n", av_err2str(ret));
        return -1;
    }

    mVideo->dstFrame = allocVideoFrame();
    if (!mVideo->dstFrame) {
        av_log(nullptr, AV_LOG_FATAL, "src allocVideoFrame() failed!!!\n");
        return -1;
    }

    mVideo->srcFrame = allocVideoFrame();
    if (!mVideo->srcFrame) {
        av_log(nullptr, AV_LOG_FATAL, "dst allocVideoFrame() failed!!!\n");
        return -1;
    }

    av_log(nullptr, AV_LOG_FATAL, "[Exporter] open video stream\n");

    return 0;
}

AVFrame *GHExporter::allocAudioFrame(int sampleCount, int sampleRate, uint64_t channelLayout, AVSampleFormat format) {

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        av_log(nullptr, AV_LOG_FATAL, "av_frame_alloc() failed!!!\n");
        return nullptr;
    }

    frame->nb_samples = sampleCount;
    frame->sample_rate = sampleRate;
    frame->channel_layout = channelLayout;
    frame->format = format;

    if (sampleCount > 0) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_log(nullptr, AV_LOG_FATAL, "av_frame_get_buffer() failed!!!\n");
            return nullptr;
        }
    }

    return frame;
}

AVFrame *GHExporter::allocVideoFrame() {

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        av_log(nullptr, AV_LOG_FATAL, "av_frame_alloc() failed!!!\n");
        return nullptr;
    }

    frame->width = mWidth;
    frame->height = mHeight;
    frame->format = STREAM_PIX_FMT;

    if (av_frame_get_buffer(frame, 32) < 0) {
        av_log(nullptr, AV_LOG_FATAL, "av_frame_get_buffer() failed!!!\n");
        return nullptr;
    }

    return frame;
}

bool GHExporter::writeAudioFrame() {
    // data and size must be 0;
    AVPacket pkt = { 0 };
    int ret;
    int got_packet;
    int dst_nb_samples;

    av_init_packet(&pkt);
    AVCodecContext* codecCtx = mAudio->codecCtx;

    ret = getNextAudioFrame();

    if (ret >= 0) {
        dst_nb_samples = av_rescale_rnd(swr_get_delay(mAudio->swrCtx, codecCtx->sample_rate) + mAudio->srcFrame->nb_samples,
                                        codecCtx->sample_rate, codecCtx->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == mAudio->srcFrame->nb_samples);

        ret = av_frame_make_writable(mAudio->dstFrame);
        if (ret < 0) {

            return false;
        }

        ret = swr_convert(mAudio->swrCtx,
                          mAudio->dstFrame->data, dst_nb_samples,
                          (const uint8_t **)mAudio->srcFrame->data, mAudio->srcFrame->nb_samples);
        if (ret < 0) {
            return false;
        }

        mAudio->dstFrame->pts = av_rescale_q(mAudio->samplesCount, (AVRational){1, codecCtx->sample_rate}, codecCtx->time_base);
        mAudio->samplesCount += dst_nb_samples;
    }

    ret = avcodec_encode_audio2(codecCtx, &pkt, mAudio->dstFrame, &got_packet);
    if (ret < 0) {
        return false;
    }

    if (got_packet) {
        av_packet_rescale_ts(&pkt, codecCtx->time_base, mAudio->stream->time_base);
        pkt.stream_index = mAudio->stream->index;
        logPacket(&pkt);
        ret = av_interleaved_write_frame(mFormatCtx, &pkt);

        if (ret < 0) {
            return false;
        }
    }

    return ret >= 0 || got_packet;
}

bool GHExporter::writeVideoFrame() {

    int ret;
    AVCodecContext *codecCtx = mVideo->codecCtx;

    AVPacket pkt;
    av_init_packet(&pkt);

    ret = getNextVideoFrame();
    if (ret < 0) {
        return false;
    }

    ret = avcodec_send_frame(codecCtx, mVideo->dstFrame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        av_log(nullptr, AV_LOG_FATAL, "avcodec_send_frame() error: %s\n", av_err2str(ret));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx, &pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                return true;
            }
            av_log(nullptr, AV_LOG_FATAL, "avcodec_receive_packet() error: %s\n", av_err2str(ret));
            return false;

        } else {
            av_packet_rescale_ts(&pkt, mVideo->codecCtx->time_base, mVideo->stream->time_base);
            pkt.stream_index = mVideo->stream->index;
            logPacket(&pkt);
            ret = av_interleaved_write_frame(mFormatCtx, &pkt);
            if (ret < 0) {
                av_log(nullptr, AV_LOG_FATAL, "av_interleaved_write_frame() error: %s\n", av_err2str(ret));
                return false;
            } else {
                mVideoWrittenCount++;
            }
        }
    }
    return false;
}

int GHExporter::getNextVideoFrame() {

    if (av_compare_ts(mVideo->nextPts, mVideo->codecCtx->time_base, STREAM_DURATION, {1, 1}) > 0) {
        return -1;
    }

    if (av_frame_make_writable(mVideo->dstFrame) < 0) {
        return -1;
    }

    fillVideoPixels(mVideo->dstFrame, mWidth, mHeight);

    mVideo->dstFrame->pts = mVideo->nextPts++;

    return 0;
}

int GHExporter::getNextAudioFrame() {

    if (av_compare_ts(mAudio->nextPts, mAudio->codecCtx->time_base, STREAM_DURATION, {1, 1}) > 0) {
        return -1;
    }

    fillAudioSamples(mAudio->srcFrame);

    mAudio->srcFrame->pts = mAudio->nextPts;
    mAudio->nextPts += mAudio->srcFrame->nb_samples;

    return 0;
}

void GHExporter::closeStream(OutputStream *ost) {
    if (!ost) {
        return;
    }

    if (ost->codecCtx) {
        avcodec_free_context(&ost->codecCtx);
        ost->codecCtx = nullptr;
    }

    if (ost->dstFrame) {
        av_frame_free(&ost->dstFrame);
        ost->dstFrame = nullptr;
    }

    if (ost->srcFrame) {
        av_frame_free(&ost->srcFrame);
        ost->srcFrame = nullptr;
    }

    if (ost->swsCtx) {
        sws_freeContext(ost->swsCtx);
        ost->swsCtx = nullptr;
    }

    if (ost->swrCtx) {
        swr_free(&ost->swrCtx);
        ost->swrCtx = nullptr;
    }
}

static int gCount = 0;

void GHExporter::fillVideoPixels(AVFrame *frame, int width, int height) {
    gCount++;

    /* Y */
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            frame->data[0][y * frame->linesize[0] + x] = x + y + gCount * 3;

    /* Cb and Cr */
    for (int y = 0; y < height / 2; y++) {
        for (int x = 0; x < width / 2; x++) {
            frame->data[1][y * frame->linesize[1] + x] = 128 + y + gCount * 2;
            frame->data[2][y * frame->linesize[2] + x] = 64 + x + gCount * 5;
        }
    }
}

void GHExporter::fillAudioSamples(AVFrame *frame) {
    int value;
    int16_t *q = (int16_t *) frame->data[0];
    for (int j = 0; j < frame->nb_samples; j++) {
        value = (int) (sin(mAudio->t) * 10000);
        for (int i = 0; i < mAudio->codecCtx->channels; i++)
            *q++ = value;
        mAudio->t += mAudio->tincr;
        mAudio->tincr += mAudio->tincr2;
    }
}

void GHExporter::logPacket(const AVPacket *pkt) {
    AVRational *time_base = &mFormatCtx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
