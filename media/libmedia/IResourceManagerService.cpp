/*
**
** Copyright (c) 2013, The Linux Foundation. All rights reserved.
** Not a Contribution
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**	   http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>  // for status_t
#include <binder/Parcel.h>
#include <binder/IMemory.h>
#include <IResourceManagerService.h>
#include <ResourceManagerService.h>

#define LOG_TAG "IResourceManagerService"
//#define LOG_NDEBUG 0

namespace android {

enum {
    SET_PARAM = IBinder::FIRST_CALL_TRANSACTION,
};

class BpResourceManagerService: public BpInterface<IResourceManagerService>
{
public:
    BpResourceManagerService(const sp<IBinder>& impl)
        : BpInterface<IResourceManagerService>(impl)
    {
       ALOGV("BpResourceManagerService::constructor- impl=%p, this=%p",
              impl.get(), this);
    }
    ~BpResourceManagerService()
    {
       ALOGV("BpResourceManagerService::destructor, this=%p", this);
    }

    virtual status_t setParam(const String8& keyValuePairs)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IResourceManagerService::getInterfaceDescriptor());
        data.writeString8(keyValuePairs);
        remote()->transact(SET_PARAM, data, &reply);
        return reply.readInt32();
    }
};

IMPLEMENT_META_INTERFACE(ResourceManagerService, "ResourceManagerService");

// ----------------------------------------------------------------------

status_t BnResourceManagerService::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    status_t status = NO_ERROR;
    switch (code) {
        case SET_PARAM :
        {
            CHECK_INTERFACE(IResourceManagerService, data, reply);
            String8 keyValuePairs(data.readString8());
            reply->writeInt32(setParam(keyValuePairs));
            return NO_ERROR;
        } break;
        default:
        {
            ALOGI("BnResourceManagerService::onTransact received unrecognized msg %d",code);
            return BBinder::onTransact(code, data, reply, flags);
        } break;
    }
    return status;
}

// ----------------------------------------------------------------------------

}; // namespace android
