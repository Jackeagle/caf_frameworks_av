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


#ifndef ANDROID_IRESOURCEMANAGERSERVICE_H
#define ANDROID_IRESOURCEMANAGERSERVICE_H

#include <utils/Errors.h>  // for status_t
#include <utils/KeyedVector.h>
#include <utils/RefBase.h>
#include <utils/String8.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>


namespace android {

class IResourceManagerService: public IInterface
{
public:
    DECLARE_META_INTERFACE(ResourceManagerService);
    /*
     * Methods executed thru BpResourceManagerService onTransact
     * These virtual functions must be implemented in ResourceManagerService
     */
    virtual status_t setParam(
             const String8& keyValuePairs) = 0;

};

// ----------------------------------------------------------------------------

class BnResourceManagerService: public BnInterface<IResourceManagerService>
{
public:
    virtual status_t    onTransact( uint32_t code,
                                    const Parcel& data,
                                    Parcel* reply,
                                    uint32_t flags = 0);
};

}; // namespace android

#endif // ANDROID_IRESOURCEMANAGERSERVICE_H
