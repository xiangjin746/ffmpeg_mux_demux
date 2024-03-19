

#include "demux.h"

#define ADTS_HEADER_LEN  7;

#define fftime_to_milliseconds(ts) (av_rescale(ts, 1000, AV_TIME_BASE))
#define milliseconds_to_fftime(ms) (av_rescale(ms, AV_TIME_BASE, 1000))

#define DEMUXER_INIT() (struct demuxer) {\
						.mutex = PTHREAD_MUTEX_INITIALIZER,\
						.is_open = 0,\
						.is_seek = 0,\
						.fmt_ctx = NULL,\
						.video_stream_idx = -1,\
						.audio_stream_idx = -1,\
						.pkt = {0},\
						.last_pkt = {0},\
						.st = NULL,\
						.bsf_ctx = NULL,\
						.is_end= 0,\
						.start_time = 0,\
						.secs = 0,\
						.duration = 0,\
						.fps = 1.,\
					}

const int sampling_frequencies[] = {
    96000,  // 0x0
    88200,  // 0x1
    64000,  // 0x2
    48000,  // 0x3
    44100,  // 0x4
    32000,  // 0x5
    24000,  // 0x6
    22050,  // 0x7
    16000,  // 0x8
    12000,  // 0x9
    11025,  // 0xa
    8000   // 0xb
    // 0xc d e f是保留的
};

int adts_header(char * const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels)
{

    int sampling_frequency_index = 3; // 默认使用48000hz
    int adtsLen = data_length + 7;

    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for(i = 0; i < frequencies_size; i++)
    {
        if(sampling_frequencies[i] == samplerate)
        {
            sampling_frequency_index = i;
            break;
        }
    }
    if(i >= frequencies_size)
    {
        printf("unsupport samplerate:%d\n", samplerate);
        return -1;
    }

    p_adts_header[0] = 0xff;         //syncword:0xfff                          高8bits
    p_adts_header[1] = 0xf0;         //syncword:0xfff                          低4bits
    p_adts_header[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    p_adts_header[1] |= (0 << 1);    //Layer:0                                 2bits
    p_adts_header[1] |= 1;           //protection absent:1                     1bit

    p_adts_header[2] = (profile)<<6;            //profile:profile               2bits
    p_adts_header[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits
    p_adts_header[2] |= (0 << 1);             //private bit:0                   1bit
    p_adts_header[2] |= (channels & 0x04)>>2; //channel configuration:channels  高1bit

    p_adts_header[3] = (channels & 0x03)<<6; //channel configuration:channels 低2bits
    p_adts_header[3] |= (0 << 5);               //original：0                1bit
    p_adts_header[3] |= (0 << 4);               //home：0                    1bit
    p_adts_header[3] |= (0 << 3);               //copyright id bit：0        1bit
    p_adts_header[3] |= (0 << 2);               //copyright id start：0      1bit
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    p_adts_header[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    p_adts_header[6] = 0xfc;      //‭11111100‬       //buffer fullness:0x7ff 低6bits
    // number_of_raw_data_blocks_in_frame：
    //    表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧。

    return 0;
}


demuxer_t *demuxer_create(void)
{
    demuxer_t *demuxer = calloc(1, sizeof(demuxer_t));
    if(demuxer != NULL) {
        *demuxer = DEMUXER_INIT();
    }

    return demuxer;
}

void demuxer_destroy(demuxer_t **demuxer)
{
    if(demuxer != NULL && *demuxer != NULL) {
        demuxer_close(*demuxer);
        free(*demuxer);
        *demuxer = NULL;
    }
}

static void __demuxer_reinit(demuxer_t *demuxer)
{
	if (demuxer->fmt_ctx != NULL) {
		avformat_close_input(&demuxer->fmt_ctx);
		demuxer->fmt_ctx = NULL;
	}

	if (demuxer->bsf_ctx) {
		av_bsf_free(&demuxer->bsf_ctx);
		demuxer->bsf_ctx = NULL;
	}

    demuxer->video_stream_idx = -1;
	demuxer->audio_stream_idx = -1;
	av_packet_unref(&demuxer->pkt);
	av_packet_unref(&demuxer->last_pkt);
	demuxer->is_end = 0;
	demuxer->is_seek = 0;
	demuxer->secs = 0;
	demuxer->duration = 0;
	demuxer->fps = 1.;
}

int demuxer_open(demuxer_t *demuxer, const char *filename)
{
    int ret = -1;
    const AVBitStreamFilter *filter = NULL;

    // 1. 检查参数有效性
    if(demuxer == NULL || filename == NULL || *filename == '\0') {
        fprintf(stderr, "demuxer_open arg error.\n");
        return ret;
    }

    // 2. 加锁
    if (pthread_mutex_lock(&demuxer->mutex) != 0) {
        fprintf(stderr, "Failed to acquire mutex.\n");
        return ret;
    }

    // 3. 初始化与重置
    if(demuxer->is_open == 0) {
        __demuxer_reinit(demuxer);

        // 4. 打开媒体文件
        ret = avformat_open_input(&demuxer->fmt_ctx, filename, NULL, NULL);
        if (ret != 0) {
            fprintf(stderr, "avformat_open_input failed for filename '%s'\n", filename);
            goto fail;
        }

        // 5. 查找流信息
        ret = avformat_find_stream_info(demuxer->fmt_ctx, NULL);
        if (ret != 0) {
            fprintf(stderr, "avformat_find_stream_info failed for filename '%s'\n", filename);
            goto fail;
        }

        // 6. 打印格式信息
        av_dump_format(demuxer->fmt_ctx, 0, filename, 0);

        // 7. 查找最佳视频流
        ret = av_find_best_stream(demuxer->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to find best video stream.\n");
            goto fail;
        }

        // 8. 设置视频流索引和帧率
        demuxer->video_stream_idx = ret;
        demuxer->st = demuxer->fmt_ctx->streams[demuxer->video_stream_idx];
        if (demuxer->st->avg_frame_rate.den != 0) {
            demuxer->fps = av_q2d(demuxer->st->avg_frame_rate);
        } else {
            fprintf(stderr, "Warning: Average frame rate is invalid.\n");
            demuxer->fps = 0.0;
        }

        // 9. 查找最佳音频流
        ret = av_find_best_stream(demuxer->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        if (ret >= 0) {
            demuxer->audio_stream_idx = ret;
        } else {
            fprintf(stderr, "Failed to find best audio stream. Continuing without audio.\n");
            goto fail;
        }

        // 10. 应用过滤器
        if (strstr(demuxer->fmt_ctx->iformat->name, "mp4") != NULL) {
            enum AVCodecID codec_id = demuxer->fmt_ctx->streams[demuxer->video_stream_idx]->codecpar->codec_id;

            if (codec_id == AV_CODEC_ID_H264) {
                filter = av_bsf_get_by_name("h264_mp4toannexb");
            } else if (codec_id == AV_CODEC_ID_HEVC) {
                filter = av_bsf_get_by_name("hevc_mp4toannexb");
            }

            if (filter) {
                if (av_bsf_alloc(filter, &demuxer->bsf_ctx) == 0) {
                    avcodec_parameters_copy(demuxer->bsf_ctx->par_in, demuxer->fmt_ctx->streams[demuxer->video_stream_idx]->codecpar);
                    if (av_bsf_init(demuxer->bsf_ctx) < 0) {
                        av_bsf_free(&demuxer->bsf_ctx);
                        fprintf(stderr, "Failed to initialize bitstream filter.\n");
                        goto fail;
                    }
                } else {
                    fprintf(stderr, "Failed to allocate memory for bitstream filter.\n");
                    goto fail;
                }
            }
        }

        // 11. 计算视频持续时间
        if (demuxer->fmt_ctx->duration != AV_NOPTS_VALUE) {
            demuxer->duration = demuxer->fmt_ctx->duration + (demuxer->fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
            demuxer->secs  = fftime_to_milliseconds(demuxer->duration);
        } else {
            fprintf(stderr, "Warning: Duration value is invalid.\n");
            demuxer->duration = 0.0;
        }
        printf("'%s' total duration secs: %"PRId64"\n", filename, demuxer->secs);

        // 12. 设置开始时间
        demuxer->start_time = (demuxer->fmt_ctx->start_time != AV_NOPTS_VALUE) ? demuxer->fmt_ctx->start_time / AV_TIME_BASE : 0.0;

        // 13. 更新打开状态
        demuxer->is_open = 1;
    }

    // 14. 解锁并返回
    pthread_mutex_unlock(&demuxer->mutex);
    return 0;

fail:
    if (demuxer->fmt_ctx) {
        avformat_close_input(&demuxer->fmt_ctx);
    }
    pthread_mutex_unlock(&demuxer->mutex);
    return -1;
}

int demuxer_close(demuxer_t *demuxer)
{
    int ret = 0;

    // 1. 检查参数有效性
    if (demuxer == NULL) {
        fprintf(stderr, "demuxer_close arg error.\n");
        return -1;
    }

    // 2. 加锁
    if (pthread_mutex_lock(&demuxer->mutex) != 0) {
        fprintf(stderr, "Failed to acquire mutex.\n");
        return -1;
    }

    // 3. 检查是否已打开
    if (demuxer->is_open) {
        // 4. 关闭输入上下文
        if (demuxer->fmt_ctx) {
            avformat_close_input(&demuxer->fmt_ctx);
            demuxer->fmt_ctx = NULL;
        }

        // 5. 释放过滤器上下文
        if (demuxer->bsf_ctx) {
            av_bsf_free(&demuxer->bsf_ctx);
            demuxer->bsf_ctx = NULL;
        }

        av_packet_unref(&demuxer->pkt);
		av_packet_unref(&demuxer->last_pkt);

        // 6. 重置开启标志
        demuxer->is_open = 0;
        demuxer->is_end = 0;
		demuxer->is_seek = 0;
    }

    // 7. 解锁并返回
    pthread_mutex_unlock(&demuxer->mutex);
    return ret;
}

int demuxer_seek(demuxer_t *demuxer, int64_t m)
{
    
    int ret = -2;// 1. 初始化返回值`ret`为-2，表示默认的错误状态。
	int64_t seek_pos = milliseconds_to_fftime(m);// 2. 将`m`（毫秒）转换为对应的FFmpeg时间单位，存储在`seek_pos`中。
    int64_t duration = -1;
	int seek_by_bytes = 0;

    // 3. 检查`demuxer`指针是否为`NULL`，如果是，则立即返回-1。
    if (demuxer == NULL){
        fprintf(stderr, "demuxer_seek arg error.\n");
        return -1;
    }
        
    // 4. 锁定`demuxer`的互斥锁，保证线程安全。
    pthread_mutex_lock(&demuxer->mutex);

    // 5. 获取媒体持续时间，并将其从毫秒转换为FFmpeg的时间单位，存储在`duration`变量中。
    duration = milliseconds_to_fftime(demuxer->secs);
    
    // 6. 判断是否通过字节进行跳转，这依赖于demuxer的格式上下文的`iformat`标志。
    seek_by_bytes = !!(demuxer->fmt_ctx->iformat->flags & AVFMT_TS_DISCONT);

    // 7. 如果解复用器是打开的状态（`is_open`> 0），根据是否通过字节跳转执行不同的逻辑：
    if (demuxer->is_open > 0)
    {
        // 如果不通过字节跳转（`seek_by_bytes`为0）
        if (!seek_by_bytes) {
            if (seek_pos < duration) {
                ret = avformat_seek_file(demuxer->fmt_ctx, demuxer->video_stream_idx, INT64_MIN, seek_pos, INT64_MAX, 0);
                if (ret < 0) {
                    fprintf(stderr, "avformat_seek_file (time-based seek) failed: %s\n", av_err2str(ret));
                    ret = -4;
                } else {
                    ret = 0;
                }
            } else {
                fprintf(stderr, "Seek position (%"PRId64") exceeds duration (%"PRId64")\n", seek_pos, duration);
                ret = -3;
            }
        } else {
            double pos = avio_tell(demuxer->fmt_ctx->pb);
            m /= 1000;

            if (demuxer->fmt_ctx->bit_rate) {
                m *= demuxer->fmt_ctx->bit_rate / 8.0;
            } else {
                fprintf(stderr, "Warning: Bit rate information not available, using default value (600000 bytes/s)\n");
                m *= 600000;
            }

            pos += m;
            int64_t file_size = avio_size(demuxer->fmt_ctx->pb);

            if (pos < file_size) {
                ret = avformat_seek_file(demuxer->fmt_ctx, demuxer->video_stream_idx, INT64_MIN, pos, INT64_MAX, AVSEEK_FLAG_BYTE);
                if (ret < 0) {
                    fprintf(stderr, "avformat_seek_file (byte-based seek) failed: %s\n", av_err2str(ret));
                    ret = -4;
                } else {
                    ret = 0;
                }
            } else {
                fprintf(stderr, "Seek position (%" PRId64 ") exceeds file size (%" PRId64 ")\n", (int64_t)pos, file_size);
                ret = -3;
            }
        }
    }
    
    // 8. 设置`is_seek`标志为1，表示已执行跳转操作。
    demuxer->is_seek = 1;
    // 9. 解锁`demuxer`的互斥锁。
    pthread_mutex_unlock(&demuxer->mutex);
    // 10. 返回`ret`作为函数执行结果。
    return ret;
}

int demuxer_read(demuxer_t *demuxer, void **data, int *len, int *is_video, int *is_key, int *total, int *cur)
{
    // 参数校验
    if (demuxer == NULL) {
        fprintf(stderr, "Invalid arguments\n");
        return -1;
    }

    int ret = -2;

    pthread_mutex_lock(&demuxer->mutex);

    if (demuxer->is_open <= 0) {
        fprintf(stderr, "Demuxer is not open\n");
        ret = -4; // 新增错误码表示demuxer未打开
        goto unlock_and_fail;
    }

    do {
        av_packet_unref(&demuxer->pkt);

        if (demuxer->is_seek > 0) {
            while ((ret = av_read_frame(demuxer->fmt_ctx, &demuxer->pkt)) >= 0) {
                if (demuxer->video_stream_idx == demuxer->pkt.stream_index && demuxer->pkt.flags & AV_PKT_FLAG_KEY)
                    break;
                else
                    av_packet_unref(&demuxer->pkt);
            }

            if (ret < 0) {
                fprintf(stderr, "Seeking error or end of file reached\n");
                ret = (demuxer->last_pkt.data == NULL) ? -3 : ret;
                goto unlock_and_fail;
            }
        } else {
            ret = av_read_frame(demuxer->fmt_ctx, &demuxer->pkt);
            if (ret < 0) {
                fprintf(stderr, "Read frame error or end of file reached\n");
                goto handle_eof;
            }
        }

        if (ret >= 0) {
            demuxer->st = demuxer->fmt_ctx->streams[demuxer->pkt.stream_index];
            *is_key = demuxer->pkt.flags & AV_PKT_FLAG_KEY;
            *data = demuxer->pkt.data;
            *len = demuxer->pkt.size;
            *total = demuxer->secs;
            *cur = av_rescale_q(demuxer->pkt.pts, demuxer->st->time_base, AV_TIME_BASE_Q) / 1000;

            if (demuxer->pkt.stream_index == demuxer->video_stream_idx) {
                // 视频帧处理逻辑
                if (demuxer->bsf_ctx) {
                    if ((ret = av_bsf_send_packet(demuxer->bsf_ctx, &demuxer->pkt)) == 0) {
                        if ((ret = av_bsf_receive_packet(demuxer->bsf_ctx, &demuxer->pkt)) == 0) {
                            *data = demuxer->pkt.data;
                            *len = demuxer->pkt.size;
                        } else {
                            fprintf(stderr, "BSF receive packet failed\n");
                        }
                    } else {
                        fprintf(stderr, "BSF send packet failed\n");
                    }
                }
                *is_video = 1;
                break;
            } else if (demuxer->pkt.stream_index == demuxer->audio_stream_idx) {
                *is_video = 0;
                break;
            }
        }
    } while (1);

    pthread_mutex_unlock(&demuxer->mutex);
    return ret;

handle_eof:
    if (!demuxer->is_end) {
        // 处理文件结束逻辑
        *total = demuxer->secs;
        *cur = demuxer->secs;
        *is_key = demuxer->last_pkt.flags & AV_PKT_FLAG_KEY;
        *data = demuxer->last_pkt.data;
        *len = demuxer->last_pkt.size;
        *is_video = 1;
        demuxer->is_end = 1;
        ret = 0;
    } else {
        fprintf(stderr, "End of file already processed\n");
        ret = -5; // 新增错误码表示文件已经处理结束
    }

unlock_and_fail:
    pthread_mutex_unlock(&demuxer->mutex);
    return ret;
}

int64_t demuxer_get_duration(const char *filename)
{
    int secs = 0;
    AVFormatContext *context = NULL;
    int duration = 0;

    if (filename == NULL || *filename == '\0'){
        return 0;
    }

    if (avformat_open_input(&context, filename, NULL, NULL) < 0) {
        fprintf(stderr, "avformat_open_input failed for filename '%s'\n", filename);
        return -1;
    }

    if(avformat_find_stream_info(context,NULL) < 0){
        fprintf(stderr, "avformat_find_stream_info failed for filename '%s'\n", filename);
        avformat_close_input(&context);
        return -1;
    }

    if(context->duration != AV_NOPTS_VALUE){
        duration = context->duration + (context->duration <= INT64_MAX - 5000?5000:0);

        secs = fftime_to_milliseconds(duration);
    }

    avformat_close_input(&context);

    return secs;
}


