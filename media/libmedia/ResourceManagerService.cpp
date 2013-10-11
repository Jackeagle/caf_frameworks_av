/*
**
** Copyright (c) 2013, The Linux Foundation. All rights reserved.
** Not a Contribution.
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/


#define LOG_TAG "ResourceManagerService"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include <ResourceManagerService.h>
#include <binder/IServiceManager.h>
#include <media/AudioSystem.h>
#include <binder/IPCThreadState.h>

namespace android {

// Static variables
uint16_t       ResourceManagerService::mNumInstances = 0;
ResourceManagerService* ResourceManagerService::mSelf = (ResourceManagerService*)NULL;

/*
 * Constructor
 */
ResourceManagerService::ResourceManagerService()
{
    ALOGV("ResourceManagerService constructor entered, this = %p", this);
}

/*
 * Destructor
 */
ResourceManagerService::~ResourceManagerService()
{
    ALOGV("ResourceManagerService  destructor entered, this = %p", this);
}


/*
 * Get reference to ResourceManagerService singleton
 *
 * Return - ptr to ResourceManagerService class object
 */
ResourceManagerService* ResourceManagerService::getInstance()
{
    status_t status = OK;
    ALOGV("getInstance entered, mNumInstances currently %d", mNumInstances );
    if (0 == mNumInstances) {
       mSelf = new ResourceManagerService();
       if (NULL == mSelf) {
          ALOGE("getInstance: ERROR new ResourceManagerService() failed");
          return NULL;
       }
    }
    mNumInstances++;
    ALOGV("getInstance returns %p, mNumInstances set to %d", mSelf, mNumInstances);
    return mSelf;
}

/*
 * Release an instance of ResourceManagerService
 */
void ResourceManagerService::releaseInstance()
{
    ALOGV("releaseInstance entered, mNumInstances currently %d", mNumInstances );
    mNumInstances--;
    if (0 == mNumInstances) {
        delete mSelf;
    }
    ALOGV("releaseInstance returns, mNumInstances set to %d", mNumInstances);
    return;
}

/*
 * Create ResourceManagerService
 *
 * Called by Server Manager to instatiate singleton ResourceManagerService during boot-up.
 * Adds "ResourceManager.service" as a client of Server Manager
 */
void ResourceManagerService::instantiate()
{
    ALOGV("instantiate entered");
    ResourceManagerService* singleton = ResourceManagerService::getInstance();
    // Add this ResourceManager Service as client to Service Manager
    ALOGV("instantiate: call addService");
    defaultServiceManager()->addService(
            String16("resourcemanager.service"), singleton);
    ALOGV("instantiate returns");
    return;
}

/*
   * Set ResourceManager parameter
   *
   *  set audio hardware parameters. The function accepts a list of parameters
   *  Param [in]  keyValuePairs - To set hardware param
   *  key value pairs in the form: key1=value1;key2=value2;...
   *
   *  Return - errors
   *	  INVALID_OPERATION - if setparam is faild
   *	  NOERROR  - on success
   */
status_t ResourceManagerService::setParam(
              const String8& keyValuePairs)
{
    status_t aHALstatus;
    ALOGV("\n ABOUT TO call audiosystem setparam from ResourceManager \n");
    int64_t token = IPCThreadState::self()->clearCallingIdentity();
    aHALstatus = AudioSystem::setParameters(0, keyValuePairs);
    return aHALstatus;
}

} // namespace android
