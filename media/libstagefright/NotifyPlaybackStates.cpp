/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
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
    property_get("use.dts_m6_notify", prop, "0");
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

void NotifyPlaybackStates::notify_playback_state(int sessionId,
                                                 int streamType,
                                                 const char *mime,
                                                 int sampleRate,
                                                 int channels,
                                                 bool isPlaying,
                                                 bool isHpxPreprocessed)
{
    char prop[PROPERTY_VALUE_MAX];
    char path[PATH_MAX];
    char value[MAX_LENGTH_OF_INTEGER_IN_STRING];
    char buf[1024];
    int fd;
    property_get("use.dts_m6_notify", prop, "0");
    if ((!strncmp("true", prop, sizeof("true")) || atoi(prop)) &&
        (streamType == AUDIO_STREAM_MUSIC) && (sessionId)) {
        ALOGV("notify_playback_state - isPlaying: %d", isPlaying);
        strlcpy(path, STATE_NOTIFY_FILE, sizeof(path));
        snprintf(value, sizeof(value), "%d", sessionId);
        strlcat(path, value, sizeof(path));
        if ((fd=open(path, O_TRUNC|O_WRONLY)) < 0) {
            ALOGE("Open state notifier node failed");
        } else {
            if (mime == NULL) {
                ALOGE("mime is NULL, write failed");
                close(fd);
                return;
            }
            snprintf(buf, sizeof(buf), "mime=%s;sample_rate=%d;channel_mode=%d;playback_state=%d;hpx_processed=%d",
                     mime, sampleRate, channels, isPlaying, isHpxPreprocessed);
            int n = write(fd, buf, strlen(buf));
            if (n > 0)
                ALOGV("Write to state notifier node successful, bytes written: %d", n);
            else
                ALOGE("Write state notifier node failed");
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
    property_get("use.dts_m6_notify", prop, "0");
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
