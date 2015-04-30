/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef __DRM_MANAGER_CLIENT_H__
#define __DRM_MANAGER_CLIENT_H__

#include <utils/threads.h>
#include <binder/IInterface.h>
#include "drm_framework_common.h"

namespace android {

class DrmInfo;
class DrmRights;
class DrmMetadata;
class DrmInfoEvent;
class DrmInfoStatus;
class DrmInfoRequest;
class DrmSupportInfo;
class DrmConstraints;
class DrmConvertedStatus;
class DrmManagerClientImpl;

/**
 * The Native application will instantiate this class and access DRM Framework
 * services through this class.
 *
 */
class DrmManagerClient {
public:
/**
 * APIs which will be used by native modules (e.g. StageFright)
 *
 */
public:
    /**
     * Open the decrypt session to decrypt the given protected content
     *
     * @param[in] fd File descriptor of the protected content to be decrypted
     * @param[in] offset Start position of the content
     * @param[in] length The length of the protected content
     * @param[in] mime Mime type of the protected content if it is not NULL or empty
     * @return
     *     Handle for the decryption session
     */
    sp<DecryptHandle> openDecryptSession(int fd, off64_t offset, off64_t length, const char* mime)
    {
      return NULL;
    }

    /**
     * Open the decrypt session to decrypt the given protected content
     *
     * @param[in] uri Path of the protected content to be decrypted
     * @param[in] mime Mime type of the protected content if it is not NULL or empty
     * @return
     *     Handle for the decryption session
     */
    sp<DecryptHandle> openDecryptSession(const char* uri, const char* mime)
    {
      return NULL;
    }

    /**
     * Open the decrypt session to decrypt the given protected content
     *
     * @param[in] buf Data to initiate decrypt session
     * @param[in] mimeType Mime type of the protected content
     * @return
     *     Handle for the decryption session
     */
    sp<DecryptHandle> openDecryptSession(const DrmBuffer& buf, const String8& mimeType)
    {
      return NULL;
    }

    /**
     * Close the decrypt session for the given handle
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @return status_t
     *     Returns DRM_NO_ERROR for success, DRM_ERROR_UNKNOWN for failure
     */
    status_t closeDecryptSession(sp<DecryptHandle> &decryptHandle)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Consumes the rights for a content.
     * If the reserve parameter is true the rights is reserved until the same
     * application calls this api again with the reserve parameter set to false.
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[in] action Action to perform. (Action::DEFAULT, Action::PLAY, etc)
     * @param[in] reserve True if the rights should be reserved.
     * @return status_t
     *     Returns DRM_NO_ERROR for success, DRM_ERROR_UNKNOWN for failure.
     *     In case license has been expired, DRM_ERROR_LICENSE_EXPIRED will be returned.
     */
    status_t consumeRights(sp<DecryptHandle> &decryptHandle, int action, bool reserve)
    {
      return DRM_ERROR_UNKNOWN;
    }

    /**
     * Informs the DRM engine about the playback actions performed on the DRM files.
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[in] playbackStatus Playback action (Playback::START, Playback::STOP, Playback::PAUSE)
     * @param[in] position Position in the file (in milliseconds) where the start occurs.
     *                     Only valid together with Playback::START.
     * @return status_t
     *     Returns DRM_NO_ERROR for success, DRM_ERROR_UNKNOWN for failure
     */
    status_t setPlaybackStatus(
            sp<DecryptHandle> &decryptHandle, int playbackStatus, int64_t position)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Initialize decryption for the given unit of the protected content
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[in] decryptUnitId ID which specifies decryption unit, such as track ID
     * @param[in] headerInfo Information for initializing decryption of this decrypUnit
     * @return status_t
     *     Returns DRM_NO_ERROR for success, DRM_ERROR_UNKNOWN for failure
     */
    status_t initializeDecryptUnit(
            sp<DecryptHandle> &decryptHandle, int decryptUnitId, const DrmBuffer* headerInfo)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Decrypt the protected content buffers for the given unit
     * This method will be called any number of times, based on number of
     * encrypted streams received from application.
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[in] decryptUnitId ID which specifies decryption unit, such as track ID
     * @param[in] encBuffer Encrypted data block
     * @param[out] decBuffer Decrypted data block
     * @param[in] IV Optional buffer
     * @return status_t
     *     Returns the error code for this API
     *     DRM_NO_ERROR for success, and one of DRM_ERROR_UNKNOWN, DRM_ERROR_LICENSE_EXPIRED
     *     DRM_ERROR_SESSION_NOT_OPENED, DRM_ERROR_DECRYPT_UNIT_NOT_INITIALIZED,
     *     DRM_ERROR_DECRYPT for failure.
     */
    status_t decrypt(
            sp<DecryptHandle> &decryptHandle, int decryptUnitId,
            const DrmBuffer* encBuffer, DrmBuffer** decBuffer, DrmBuffer* IV = NULL)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Finalize decryption for the given unit of the protected content
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[in] decryptUnitId ID which specifies decryption unit, such as track ID
     * @return status_t
     *     Returns DRM_NO_ERROR for success, DRM_ERROR_UNKNOWN for failure
     */
    status_t finalizeDecryptUnit(
            sp<DecryptHandle> &decryptHandle, int decryptUnitId)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Reads the specified number of bytes from an open DRM file.
     *
     * @param[in] decryptHandle Handle for the decryption session
     * @param[out] buffer Reference to the buffer that should receive the read data.
     * @param[in] numBytes Number of bytes to read.
     * @param[in] offset Offset with which to update the file position.
     *
     * @return Number of bytes read. Returns -1 for Failure.
     */
    ssize_t pread(sp<DecryptHandle> &decryptHandle,
            void* buffer, ssize_t numBytes, off64_t offset)
    {
      return DRM_ERROR_CANNOT_HANDLE;
    }

    /**
     * Validates whether an action on the DRM content is allowed or not.
     *
     * @param[in] path Path of the protected content
     * @param[in] action Action to validate. (Action::DEFAULT, Action::PLAY, etc)
     * @param[in] description Detailed description of the action
     * @return true if the action is allowed.
     */
    bool validateAction(const String8& path, int action, const ActionDescription& description)
    {
      return false;
    }

    /**
     * Retrieves the mime type embedded inside the original content
     *
     * @param[in] path the path of the protected content
     * @param[in] fd the file descriptor of the protected content
     * @return String8
     *     Returns mime-type of the original content, such as "video/mpeg"
     */
    String8 getOriginalMimeType(const String8& path, int fd)
    {
      return String8::empty();
    }
};

};

#endif /* __DRM_MANAGER_CLIENT_H__ */

