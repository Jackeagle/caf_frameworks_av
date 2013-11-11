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


#ifndef ANDROID_RESOURCE_MANAGER_SERVICE_H
#define ANDROID_RESOURCE_MANAGER_SERVICE_H

#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <utils/String8.h>
#include <binder/Parcel.h>
#include <IResourceManagerService.h>

namespace android {

/*
 * ResourceManager Native Service
 *
 * Calls AudioHAL for Resourmanager requests to be sent
 * This is implemented as a Singleton Class !
 * First session that requests to be initialized, instantiates ResourceManagerService
 * The first session that has permission and requests Global Control gets to set these parameters
 * All methods are synchronous
 */
class ResourceManagerService : public BnResourceManagerService
{

  protected:
    ResourceManagerService();
  public:
    virtual   ~ResourceManagerService();

public:
    /*
     * Called by Server Manager process to instatiate single ResourceManagerService
     * Add "ResourceManager.service" as a client of Server Manager
     */
    static  void          instantiate();
    /*
     * Get this singleton instance of ResourceManagerService
     */
    static ResourceManagerService* getInstance();

    /*
     * Release instance of ResourceManagerService
     */
    void releaseInstance();

    /*
     * Set ResourceManager parameter
     *
     * The function accepts a single parameter type and its data value(s).
     *
     * Param [in]  keyValuePairs - To set hardware param
     * Return - errors
     *      INVALID_OPERATION - if setparam is faild
     *      NOERROR  - on success
     */
    virtual status_t setParam(
              const String8& keyValuePairs);


private:
    //Prevent copy-construction
    ResourceManagerService(const ResourceManagerService&);
    //Prevent assignment
    ResourceManagerService& operator=(const ResourceManagerService&);

   /*
    * Variables
    */
  private:
    static      uint16_t       mNumInstances;
    static      ResourceManagerService* mSelf;

};


}; // namespace android

#endif // ANDROID_RESOURCE_MANAGER_SERVICE_H
