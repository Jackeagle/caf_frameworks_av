/*Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */



#define LOG_NDEBUG 0
#define LOG_TAG "NotifyPlaybackStates"

#include <utils/Log.h>
#include <include/NotifyPlaybackStates.h>
#include <media/IMediaPlayerService.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <sys/stat.h>

#define STATE_NOTIFY_FILE "/data/dts/notify_plbk_session"
#define MAX_LENGTH_OF_INTEGER_IN_STRING 13

namespace android {

void NotifyPlaybackStates::create_state_notifier_node(int sessionId, int streamType)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    int fd;
    property_get("use.dts_eagle", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("create_state_notifier_node - sessionId: %d", sessionId);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));

        if ((fd=open(path, O_RDONLY)) < 0) {
            ALOGV("No File exisit");
        } else {
            ALOGV("A file with the same name exist. Remove it before creating it");
            close(fd);
            remove(path);
        }
        if ((fd=creat(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0) {
            ALOGE("opening state notifier node failed returned");
            return;
        }
        chmod(path, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH);
        ALOGV("opening state notifier node successful");
        close(fd);
    }
}

void NotifyPlaybackStates::notify_playback_state(int sessionId, int streamType, bool isPlaying)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    char buf[1024];
    int fd;
    property_get("use.dts_eagle", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("notify_playback_state - isPlaying: %d", isPlaying);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));
        if ((fd=open(path, O_TRUNC|O_WRONLY)) < 0) {
            ALOGV("Write to state notifier node failed");
        } else {
            ALOGV("Write to state notifier node successful");
            snprintf(buf, sizeof(buf), "playback_state=%d;", isPlaying);
            int n = write(fd, buf, strlen(buf));
            ALOGV("number of bytes written: %d", n);
            close(fd);
        }
    }
}

void NotifyPlaybackStates::notify_playback_config_state(int sessionId,
                                                          int streamType,
                                                          const char *mime,
                                                          int sampleRate,
                                                          int channels)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    char buf[1024];
    int fd;
    property_get("use.dts_eagle", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("notify_samplerate_channelmode_state");
        ALOGV("Sample Rate: %d, Channel Mode: %d", sampleRate, channels);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));
        if ((fd=open(path, O_TRUNC|O_WRONLY)) < 0) {
            ALOGV("Write to state notifier node failed");
        } else {
            ALOGV("Write to state notifier node successful");
            snprintf(buf, sizeof(buf), "mime=%s;sample_rate=%d;channel_mode=%d;",
                     mime, sampleRate, channels);
            int n = write(fd, buf, strlen(buf));
            ALOGV("number of bytes written: %d", n);
            close(fd);
        }
    }
}

void NotifyPlaybackStates::notify_hpx_preprocessed_state(int sessionId,
                                                           int streamType,
                                                           bool isHpxPreprocessed)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    char buf[1024];
    int fd;
    property_get("use.dts_eagle", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("notify_hpx_preprocessed_state");
        ALOGV("isHpxPreprocessed: %d", isHpxPreprocessed);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));
        if ((fd=open(path, O_TRUNC|O_WRONLY)) < 0) {
            ALOGV("Write to state notifier node failed");
        } else {
            ALOGV("Write to state notifier node successful");
            snprintf(buf, sizeof(buf), "hpx_processed=%d;", isHpxPreprocessed);
            int n = write(fd, buf, strlen(buf));
            ALOGV("number of bytes written: %d", n);
            close(fd);
        }
    }
}

void NotifyPlaybackStates::remove_state_notifier_node(int sessionId, int streamType)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    int fd;
    property_get("use.dts_eagle", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("remove_state_notifier_node: sessionId - %d", sessionId);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));
        if ((fd=open(path, O_RDONLY)) < 0) {
            ALOGV("open state notifier node failed");
        } else {
            ALOGV("open state notifier node successful");
            ALOGV("Remove the file");
            close(fd);
            remove(path);
        }
    }
}

}
