/*
 * @Author: 赵涛 244054934@qq.com
 * @Date: 2024-01-04 16:22:19
 * @LastEditors: 赵涛 244054934@qq.com
 * @LastEditTime: 2024-01-04 16:41:01
 * @FilePath: /ffmpeg_rtmppush_demo/test_demuxer.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "demux.h"
#include "mux.h"


static int quit = 0;

static void sighandler(int sig)
{
    quit = 1;
    printf("quit\n");
}

int main(void)
{
	demuxer_t *demuxer = NULL;
	void *data = NULL;
	int len = 0, is_video = 0, is_key = 0, total = 0, cur = 0;
	int ret = -1;
	muxer_t *muxer = NULL;
	int add_video = 0;

    signal(SIGINT, sighandler);

	muxer = muxer_create();
	if (muxer == NULL) {
		printf("muxer create failed");
		return -1;
	}

	demuxer = demuxer_create();
	if (demuxer == NULL) {
		printf("demxuer failed\n");
		muxer_destroy(&muxer);
		return -1;
	}

	if (demuxer_open(demuxer, "ok.mp4") != 0) {
		printf("demuxer open failed\n");
		muxer_destroy(&muxer);
		demuxer_destroy(&demuxer);
		return -1;
	}

	if (muxer_open(muxer, "out.mp4") != 0) {
		printf("muxer open failed\n");
		muxer_destroy(&muxer);
		demuxer_destroy(&demuxer);
		return -1;
	}

	printf("total duration: %lld\n", demuxer_get_duration("ok.mp4"));

	printf("open ok\n");

    for ( ;!quit ; ) {
		ret = demuxer_read(demuxer, &data, &len, &is_video, &is_key, &total, &cur);
		if (ret >= 0) {
			if (add_video == 0) {
				if(is_video && is_key) {
					muxer_add_video_and_audio(muxer, MUXER_CODEC_H264, 800, 480, data, len);
					add_video = 1;
				} else
					continue;
			}
			//printf("%s : size=%d,%d,%d\n", is_video ? "video" : "audio", len, total, cur);
			if (is_video)
				muxer_write_video(muxer, data, len, is_key, cur);
            else{
                muxer_write_audio(muxer, data, len, cur);
            }
				
		} else {
			printf("demxuer read faild: %d\n", ret);
			break;
		}

	}

	demuxer_close(demuxer);
	muxer_close(muxer);
	muxer_destroy(&muxer);
	demuxer_destroy(&demuxer);

    printf("-------main exit-------------\n");

    return 0;
}
