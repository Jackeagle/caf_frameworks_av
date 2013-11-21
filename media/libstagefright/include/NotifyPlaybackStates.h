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

#ifndef NOTIFY_PLAYBACK_STATES_H_
#define NOTIFY_PLAYBACK_STATES_H_

namespace android {

struct NotifyPlaybackStates
{
    // Create the Playback state notifier node
    static void create_state_notifier_node(int sessionId, int streamType);

    // Notify playback state
    static void notify_playback_state(int sessionId,
                                      int streamType,
                                      const char *mime,
                                      int sampleRate,
                                      int channels,
                                      bool isPlaying,
                                      bool isHpxPreprocessed);

    // Remove the playback state notifier node
    static void remove_state_notifier_node(int sessionId, int streamType);
};

}
#endif  //QC_NOTIFY_PLAYBACK_STATES_H_
