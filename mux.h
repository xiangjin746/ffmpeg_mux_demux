/*
 * @Author: 赵涛 244054934@qq.com
 * @Date: 2024-01-04 14:10:55
 * @LastEditors: 赵涛 244054934@qq.com
 * @LastEditTime: 2024-01-04 15:06:29
 * @FilePath: /ffmpeg_rtmppush_demo/muxer.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#ifndef __MUX_H
#define __MUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief mp4暂时只支持h264和h265与g711a数据封装
 */
struct muxer;
typedef struct muxer muxer_t;

enum MUXER_CODEC_ID {
    MUXER_CODEC_H265 		= 0,
    MUXER_CODEC_H264 		= 1,
};

/**
 * @brief 创建muxer
 *
 * @return muxer_t*
 */
muxer_t *muxer_create(void);

/**
 * @brief 摧毁muxer
 *
 * @param muxer
 */
void muxer_destroy(muxer_t **muxer);

/**
 * @brief 打开文件写mp4
 *
 * @param muxer: muxer_create返回值
 * @param filename:文件名字
 * @return int 0:成功 其他值失败
 */
int muxer_open(muxer_t *muxer, const char *filename);

/**
 * @brief 添加音视频流
 *   音频只支持g711a 8000 16bit mono
 */
int muxer_add_video_and_audio(muxer_t *muxer, int videocodecid, int width, int height, uint8_t *extradata, int32_t extradata_size);
/**
 * @brief 关闭mp4文件
 *
 * @param muxer:muxer_create返回值
 * @return int: 0:关闭成功　其他失败
 */
int muxer_close(muxer_t *muxer);

/**
 * @brief 写入视频数据，现在只支持h264和h265
 *
 * @param muxer: muxer_create返回值
 * @param data: 视频数据
 * @param len: 视频数据长度
 * @param keyframe: 当前视频是否为关键帧
 * @param pts:　当前视频的时间戳(精确度毫秒),如果传入-1自动产生时间戳,注意要么一直传－１要么自己传入pts,千万不要传下－１一传下自己pts
 * @return int: 0:关闭成功　其他失败
 *              -1：mxuer为NULL
 *              -2:　文件没有打开
 *              -3: 写数据失败
 */
int muxer_write_video(muxer_t *muxer, const char *data, const int len, const unsigned char keyframe, int64_t pts);

/**
 * @brief 写入音频数据,现在只支持g711a或者pcma数据
 *
 * @param muxer: muxer_create返回值
 * @param data: 音频数据
 * @param data_size: 当前音频数据大小
 * @param pts:　当前音频数据时间戳(精确度毫秒),如果传入-1自动产生时间戳,注意要么一直传－１要么自己传入pts,千万不要传下－１一传下自己pts
 * @return int: 0:关闭成功　其他失败
 *              -1：mxuer为NULL
 *              -2:　文件没有打开
 *              -3: 写数据失败
 */
int muxer_write_audio(muxer_t *muxer, const char *data, const int data_size, const int64_t pts);

#ifdef __cplusplus
}
#endif



#endif //__MUX_H
