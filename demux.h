#ifndef __DEMUX_H
#define __DEMUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <libavformat/avformat.h>
#include <pthread.h>

typedef struct demuxer{
    pthread_mutex_t mutex;
    unsigned char is_open;
    unsigned char is_seek;
    AVFormatContext *fmt_ctx;
    AVRational time_base;
    int video_stream_idx;
    int audio_stream_idx;
    AVPacket pkt;
    AVPacket last_pkt;
    AVStream *st;
    AVBSFContext *bsf_ctx;
    unsigned char is_end;
    int64_t start_time;
    int64_t secs;
    int64_t duration;
    double fps;
}demuxer_t;

int adts_header(char * const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels);

/**
 * @brief　创建dmexuer
 *
 * @return demuxer_t*
 */
demuxer_t *demuxer_create(void);

/**
 * @brief 摧毁demuxer
 *
 * @param demuxer
 */
void demuxer_destroy(demuxer_t **demuxer);

/**
 * @brief 打开mp4文件读取音视频
 *
 * @param demuxer: demuxer_create返回值
 * @param filename: mp4文件
 * @return int: 0成功 其他失败
 */
int demuxer_open(demuxer_t *demuxer, const char *filename);

/**
 * @brief 关闭mp4文件
 *
 * @param demuxer: demuxer_create返回值
 * @return int: 0成功 其他失败
 */
int demuxer_close(demuxer_t *demuxer);

/**
 * @brief seek(单位毫秒)
 *
 * @param demuxer:demuxer_create返回值
 * @param m: seek到的时间点
 * @return int: 0成功 其他失败
 */
int demuxer_seek(demuxer_t *demuxer, int64_t m);

/**
 * @brief 读取音视频
 *
 * @param demuxer: demuxer_create返回值
 * @param data:音视频数据
 * @param len：音视频数据长度
 * @param is_video:１视频　0音频
 * @param is_key：对于是视频是否关键帧
 * @param total：总时长(单位毫秒)
 * @param cur: 当前读到哪里(单位毫秒)
 * @return int: 0成功 其他读取到文件尾
 */
int demuxer_read(demuxer_t *demuxer, void **data, int *len, int *is_video, int *is_key, int *total, int *cur);

/**
 * @brief 获取总时长()
 *
 * @param filename
 * @return int64_t: >0 当前文件总时长
 */
int64_t demuxer_get_duration(const char *filename);

#ifdef __cplusplus
}
#endif

#endif //__DEMUXER_H
