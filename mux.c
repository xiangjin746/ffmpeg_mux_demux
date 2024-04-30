#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>

#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include "libavutil/avutil.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"

#include <log.h>

#include "mux.h"
#include <limits.h>

struct muxer {
    AVFormatContext *output_ctx;
    pthread_mutex_t mutex;
    int isStart;
    int video_index;
    int audio_index;
    int complete;
    //video pts
    int64_t video_total_pts;
    int64_t  video_prev_pts;

    //audio pts
    int64_t audio_total_pts;
    int64_t audio_prev_pts;

    int video_codecid;
    float fps;
    char *filename;
};

#define MUXER_INIT()                        \
    (struct muxer)                          \
    {                                       \
        .output_ctx = NULL,                 \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .isStart = 0,                       \
        .video_index = -1,                  \
        .audio_index = -1,                  \
        .complete = 0,                      \
        .video_total_pts = 0,               \
        .video_prev_pts = -1,               \
        .audio_total_pts = 0,               \
        .audio_prev_pts = -1,               \
        .video_codecid = AV_CODEC_ID_NONE,  \
        .fps = 0.,                          \
        .filename = NULL,                   \
    }

muxer_t *muxer_create(void)
{
    muxer_t *muxer = (muxer_t *)calloc(sizeof(muxer_t), 1);
    if (muxer != NULL) {
        *muxer = MUXER_INIT();
    }

    return muxer;
}

void muxer_destroy(muxer_t **muxer)
{
    if (muxer != NULL && *muxer != NULL) {
        muxer_close(*muxer);

        free(*muxer);

        *muxer = NULL;
    }
}

int muxer_open(muxer_t *muxer, const char *filename)
{
    int ret = -3, err = -1;
    AVStream *out = NULL;

    if (muxer == NULL) {
        LOG("muxer is null\n");
        return -1;
    }

    if (filename == NULL || *filename == '\0') {
        LOG("url is null string\n");
        return -2;
    }

    pthread_mutex_lock(&muxer->mutex);

    if (muxer->isStart == 0) {

        if (muxer->filename != NULL) {
            free(muxer->filename);
            muxer->filename = NULL;
        }

        muxer->filename = strdup(filename);
        if (muxer->filename == NULL) {
            LOG("strdup filename '%s'\n", muxer->filename);
            ret = -4;
            goto fail;
        }

        err = avformat_alloc_output_context2(&muxer->output_ctx, 0, "mp4", muxer->filename);
        if (err < 0 || muxer->output_ctx == NULL) {
            LOG("allo rmp output failed: '%s'\n", muxer->filename);
            ret = -5;
            goto fail;
        }

        LOG("open '%s' success\n", muxer->filename);

        ret = 0;

        muxer->isStart = 1;
    }

fail:
    pthread_mutex_unlock(&muxer->mutex);

    return ret;
}


int muxer_close(muxer_t *muxer)
{
    int ret = -1;
    int64_t err = -1;

    if (muxer != NULL) {

        pthread_mutex_lock(&muxer->mutex);

        if (muxer->isStart == 1) {

            if (muxer->output_ctx != NULL) {
                if (muxer->complete == 1) {
                    LOG("close '%s' success\n", muxer->filename);
                    av_write_trailer(muxer->output_ctx);
                    if (muxer->output_ctx != NULL && !(muxer->output_ctx->flags & AVFMT_NOFILE))
                        avio_closep(&muxer->output_ctx->pb);
                } else {
                    LOG("close '%s' error\n", muxer->filename);
                }
                avformat_free_context(muxer->output_ctx);
                muxer->output_ctx = NULL;
            }




            if (muxer->filename != NULL) {
                free(muxer->filename);
                muxer->filename = NULL;
            }

            muxer->video_index			= -1;
            muxer->audio_index			= -1;
            muxer->complete				= 0;
            muxer->video_total_pts		= 0;
            muxer->video_prev_pts		= -1;
            muxer->audio_total_pts		= 0;
            muxer->audio_prev_pts		= -1;
            muxer->video_codecid		= AV_CODEC_ID_NONE;
            muxer->fps					= 0.;

            muxer->isStart = 0;

            ret = 0;
        }

        pthread_mutex_unlock(&muxer->mutex);
    }

    return ret;
}

static inline int write_frame(muxer_t *muxer, AVPacket * pkt)
{
    int ret = -1;

    ret = av_interleaved_write_frame(muxer->output_ctx, pkt);
    if (ret != 0) {
        LOG("Error muxer pkt error: %s\n", av_err2str(ret));
    }

    return ret;
}

static inline int __muxer_write_video(muxer_t *muxer, const void *data, int32_t len, int64_t pts, const unsigned char keyframe)
{
    AVPacket pkt;
    int ret = -1;
    AVStream *out_stream = NULL;

    av_init_packet(&pkt);

    if (pts < 0)
        pts = 0;

    // printf("Original PTS: %lld\n", pts); // Print original PTS value

    pkt.flags = (keyframe) ? AV_PKT_FLAG_KEY : 0;

    if (muxer->video_prev_pts == -1) {
        muxer->video_total_pts = 0;
        muxer->video_prev_pts = pts;
    } else if ((pts - muxer->video_prev_pts) > 0) {
        muxer->video_total_pts += (pts - muxer->video_prev_pts) * 1000;
        muxer->video_prev_pts = pts;
    } else {
        muxer->video_total_pts += (int64_t)((1000000. / muxer->fps));
    }

    // printf("Calculated total PTS: %lld\n", muxer->video_total_pts); // Print calculated total PTS

    pkt.pts = muxer->video_total_pts;
    pkt.dts = muxer->video_total_pts;

    // printf("Before scaling - PTS: %lld, DTS: %lld\n", pkt.pts, pkt.dts); // Print PTS and DTS before scaling

    pkt.data = (uint8_t *)data;
    pkt.size = len;
    pkt.stream_index = muxer->video_index;
    out_stream = muxer->output_ctx->streams[muxer->video_index];

    pkt.pts = av_rescale_q_rnd(pkt.pts, (AVRational){1, 1000000}, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, (AVRational){1, 1000000}, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);

    // printf("After scaling - PTS: %lld, DTS: %lld\n", pkt.pts, pkt.dts); // Print PTS and DTS after scaling

    pkt.pos = -1;

    ret = write_frame(muxer, &pkt);
    if (ret != 0)
        ret = -3;

    av_packet_unref(&pkt);

    return ret;
}




int muxer_add_video_and_audio(muxer_t *muxer, int videocodecid, int width, int height, uint8_t *extradata, int32_t extradata_size)
{
    AVStream *out_stream = NULL;
    int ret = -1;
    int err = 0;
    AVDictionary *options = NULL;

    if (muxer == NULL)
        return -1;

    pthread_mutex_lock(&muxer->mutex);

    if (muxer->isStart == 1 && muxer->output_ctx != NULL && muxer->complete == 0) {
        out_stream = avformat_new_stream(muxer->output_ctx, NULL);
        if (out_stream == NULL) {
            LOG("Failed allocating video output stream\n");
            ret = -1;
            goto fail;
        }

        out_stream->codecpar->codec_id = videocodecid == MUXER_CODEC_H265? AV_CODEC_ID_H265 : AV_CODEC_ID_H264;
        out_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        out_stream->codecpar->format = AV_PIX_FMT_YUV420P;
        out_stream->codecpar->height = height;
        out_stream->codecpar->width = width;
        out_stream->codecpar->bit_rate = 512 * 1024;
        out_stream->time_base = (AVRational){1, 1000};
        out_stream->codecpar->video_delay = 0;

        out_stream->codecpar->codec_tag = 0;

        if (extradata != NULL && extradata_size > 0) {

            out_stream->codecpar->extradata_size = extradata_size;
            out_stream->codecpar->extradata = av_mallocz(out_stream->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);

            if (out_stream->codecpar->extradata != NULL) {
                memcpy(out_stream->codecpar->extradata, extradata, extradata_size);
            } else {
                LOG("no memory to allocate extradata\n");
                ret = -2;
                goto fail;
            }
        }


        out_stream = avformat_new_stream(muxer->output_ctx, NULL);
        if (!out_stream) {
            LOG("Failed allocating output stream\n");
            ret = -3;
            goto fail;
        }


        //音频设备默认只支持g711a,ffmpeg没有修改的话只支持aac
        // out_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
        // out_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        // out_stream->codecpar->format = AV_SAMPLE_FMT_S16;
        // out_stream->codecpar->channels = 1;
        // out_stream->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
        // out_stream->codecpar->sample_rate = 48000;
        // out_stream->codecpar->bit_rate = 64 * 1024;
        // out_stream->codecpar->frame_size = 320;
        // out_stream->time_base = (AVRational){1, 1000};
        // out_stream->codecpar->codec_tag = 0;

        out_stream->codecpar->codec_id = AV_CODEC_ID_AAC;           // AAC编解码器
        out_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;      // 音频流
        out_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;          // AAC LC 通常使用FLTP（浮点）格式
        out_stream->codecpar->channels = 2;                         // 2个音频通道（立体声）
        out_stream->codecpar->channel_layout = AV_CH_LAYOUT_STEREO; // 立体声通道布局
        out_stream->codecpar->sample_rate = 48000;                  // 采样率设为48kHz
        out_stream->codecpar->bit_rate = 128 * 1024;                // 假设比特率为128kbps，具体值可以根据需要调整
        // out_stream->codecpar->frame_size = 不需要设置这个参数，因为对于AAC，frame_size可以是可变的。
        out_stream->time_base = (AVRational){1, 1000};              // 时间基准，通常对于音频可以保持这个设置
        // out_stream->codecpar->codec_tag = 0;                     // codec_tag通常不需要手动设置，除非有特定需求




        ret = av_find_best_stream(muxer->output_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (ret < 0) {
            LOG("Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO), muxer->filename);
            ret = -4;
            goto fail;
        }

        muxer->video_index = ret;

        ret = av_find_best_stream(muxer->output_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        if (ret < 0) {
            LOG("Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO), muxer->filename);
            ret = -5;
            goto fail;
        }

        muxer->audio_index = ret;

        if (!(muxer->output_ctx->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&muxer->output_ctx->pb, muxer->filename, AVIO_FLAG_WRITE);
            if (ret < 0) {
                LOG("Could not open output file '%s'\n", muxer->filename);
                ret = -6;
                goto fail;
            }
        }

        ret = avformat_write_header(muxer->output_ctx, NULL);
        if (ret < 0) {
            LOG("Error occurred when opening output file: %s\n", av_err2str(ret));
            ret = -7;
            goto fail;
        }

        muxer->complete = 1;
        ret = 0;
    }

fail:
    pthread_mutex_unlock(&muxer->mutex);

    return ret;
}

int muxer_write_video(muxer_t *muxer, const char *data, const int len, const unsigned char keyframe, int64_t pts)
{
    int ret = -2;
    int width = 0, height = 0;

    if (pts < 0)
        pts = 0;

    if (muxer == NULL)
        return -1;

    pthread_mutex_lock(&muxer->mutex);

    if (muxer->isStart == 1 && muxer->output_ctx != NULL && muxer->complete == 1) {
        ret = __muxer_write_video(muxer, data, len, pts, keyframe);
    }

    pthread_mutex_unlock(&muxer->mutex);

    return ret;
}

static int __muxer_write_audio_pcma(muxer_t *muxer, const void *data, int32_t len, int64_t pts)
{
    int ret = -3;
    AVPacket pkt;
    AVStream *out_stream = NULL;

    if (pts < 0)
        pts = 0;

    av_init_packet(&pkt);

    if (muxer->audio_prev_pts == -1) {
        muxer->audio_total_pts = 0;
        muxer->audio_prev_pts = pts;
    } else if ((pts - muxer->audio_prev_pts) > 0) {
        muxer->audio_total_pts += (pts - muxer->audio_prev_pts) * 1000;
        muxer->audio_prev_pts = pts;
    } else {
        muxer->audio_total_pts += (len * 1000 / 8);
    }

    pkt.pts = muxer->audio_total_pts;

    pkt.dts = pkt.pts;

    pkt.data = (uint8_t *)data;

    pkt.size = len;

    pkt.stream_index = muxer->audio_index;

    out_stream = muxer->output_ctx->streams[muxer->audio_index];

    pkt.pts = av_rescale_q_rnd(pkt.pts, (AVRational){1, 1000000}, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, (AVRational){1, 1000000}, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);

    pkt.pos = -1;

    ret = write_frame(muxer, &pkt);
    if (ret != 0)
        ret = -3;

    av_packet_unref(&pkt);

    return ret;
}

int muxer_write_audio(muxer_t *muxer, const char *data, const int data_size, const int64_t pts)
{
    int ret = -2;

    if (muxer == NULL)
        return -1;

    uint8_t *aac_data = NULL;
    int aac_data_size = 0;
    int adts_header_size = 7;

    // 计算总长度
    aac_data_size = data_size + adts_header_size;

    // 分配缓冲区
    aac_data = av_malloc(aac_data_size);

    // 添加ADTS头
    adts_header(aac_data, data_size, 
                FF_PROFILE_AAC_LOW,
                48000,
                2);

    // 拷贝AAC数据
    memcpy(aac_data + adts_header_size, data, data_size);

    pthread_mutex_lock(&muxer->mutex);

    if (muxer->isStart == 1 && muxer->output_ctx != NULL && muxer->complete == 1) {
        ret = __muxer_write_audio_pcma(muxer, aac_data, aac_data_size, pts);
    }

    pthread_mutex_unlock(&muxer->mutex);

    av_free(aac_data);

    return ret;
}


