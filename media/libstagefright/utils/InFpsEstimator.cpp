/*--------------------------------------------------------------------------
Copyright (c) 2013, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/*--------------------------------------------------------------------------*/

#define ALOG_NIDEBUG 0
#define LOG_TAG "InFpsEstimator"

#include <utils/Log.h>
#include <utils/Timers.h>
#include <sys/time.h>
#include <cutils/properties.h>

#include "InFpsEstimator.h"

#if 0
#define ALOGD ALOGE
#define ALOGV ALOGE
#endif

/* Sliding window structure which stores all relavent PTS info */
typedef struct {
    /* Array which stores PTS samples */
    int64_t    nPTS[MAX_SLIDING_WIN_WIDTH];
    /* Array index of first valid PTS or oldest valid PTS */
    int        nWindowStartIndx;
    /* Array index of last valid PTS or latest valid PTS */
    int        nWindowEndIndx;
    /* Count of number of valid PTS samples in the array */
    int        nValidWindowWidth;
    /* Sum of PTS differences of all the valid PTS samples */
    int64_t    nSumPtsDiffs;
}SlidingWindow;

/* Local context */
typedef struct {
    /* Sliding window width configuration stored in the context */
    int            nSlidingWindowWidth;
    /* Sliding window structure which stores all relavent PTS info */
    SlidingWindow  sSlidingWindow;
    /* Previous PTS stored for quick calculations. Unit: Nano seconds*/
    int64_t        nPrevPts;
    /* FPS calculated based on the average PTS diff. Unit: Milli Hz*/
    int            nCurrentFps;
    /* Number of consecutive frame drops based on PTS jump*/
    int            nConsecutiveFrameDrops;
}IfeContext;

/*******************************************************************************
 Static functions
 ******************************************************************************/
static IFE_ERRORTYPE InitializeContext(    IfeContext*   pIfe);
static IFE_ERRORTYPE AddInterpolatedPts(   IfeContext*   pIfe, int64_t nPts);
static IFE_ERRORTYPE CalculateFps(         IfeContext*   pIfe, int64_t nPts);

/*******************************************************************************
 * Function: IfeCreate
 * Description: This function creates an instance of Input FPS estimator
 *
 * Input parameters:
 * ifeCrtPrms      - Pointed to IfeCreateParams structure
 *
 * Return values:
 * IFE_NO_ERROR      - Success without any errors
 * IFE_INVALID_ARGS  - Invalid input arguments/parameters
 * IFE_MEMORY_ERROR  - Insufficient memory while allocating context memory
 *
 * Notes: none
 ******************************************************************************/
IFE_ERRORTYPE IfeCreate(IN    IfeCreateParams* pIfeCrtPrms,
                        INOUT void** hIfe)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    IfeContext*   pIfe = NULL;

    ALOGD("%s: E", __func__);
    /* Null check on input arguments*/
    if(NULL == pIfeCrtPrms)
    {
        eRet = IFE_INVALID_ARGS;
        ALOGE("%s: ifeCrtPrms = Null pointer", __func__);
    }
    /* Input validation: Sliding width has to be atleast 2 to calculate the */
    /* average PTS diff and hence the fps */
    else if (( MAX_SLIDING_WIN_WIDTH < pIfeCrtPrms->nSlidingWindowWidth) ||
             ( 2 > pIfeCrtPrms->nSlidingWindowWidth))
    {
        eRet = IFE_INVALID_ARGS;
         ALOGE("%s: Invalid nSlidingWindowWidth = %d, valid range: [%d to %d]",
               __func__, pIfeCrtPrms->nSlidingWindowWidth,
               2, MAX_SLIDING_WIN_WIDTH);
    }
    /* Allocate local context, initialize the context variables and store cfg */
    else
    {
        pIfe = (IfeContext*) malloc(sizeof(IfeContext));
        InitializeContext(pIfe);
        pIfe->nSlidingWindowWidth = pIfeCrtPrms->nSlidingWindowWidth;
        if(NULL == pIfe)
        {
            eRet = IFE_MEMORY_ERROR;
            ALOGE("%s: Insufficient memory to allocate context", __func__);
        }
        else
        {
            *hIfe = (void *)pIfe;
        }
    }
    ALOGD("%s: X", __func__);
    return eRet;
} /* IfeCreate */

/*******************************************************************************
 * Function: IfeDestroy
 * Description: This function frees up any associated resources and destroys
 *              the instance of Input FPS estimator
 *
 * Input parameters:
 * hIfe      - handle to the instance of Input FPS estimator
 *
 * Return values:
 * IFE_NO_ERROR      - Success without any errors
 * IFE_INVALID_ARGS  - Invalid input arguments/parameters
 * IFE_MEMORY_ERROR  - Error while freeing context memory
 *
 * Notes: none
 ******************************************************************************/
IFE_ERRORTYPE IfeDestroy(IN void* hIfe)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    ALOGD("%s: E", __func__);
    if(NULL == hIfe)
    {
        eRet = IFE_INVALID_ARGS;
        ALOGE("%s: hIfe = Null pointer", __func__);
    }
    else
    {
        free(hIfe);
    }
    ALOGD("%s: X", __func__);
    return eRet;
}

/*******************************************************************************
 * Function: IfeAddPts
 * Description: This function adds one PTS to the IFE
 *
 * Input parameters:
 * hIfe      - handle to the instance of Input FPS estimator
 * nPts      - PTS in nano-seconds
 *
 * Return values:
 * IFE_NO_ERROR      - Success without any errors
 * IFE_INVALID_ARGS  - Invalid input arguments/parameters
 *
 * Notes: none
 ******************************************************************************/
IFE_ERRORTYPE IfeAddPts(IN void*   hIfe,
                        IN int64_t nPts)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    IfeContext*   pIfe = NULL;
    ALOGE("%s: E PTS %lld", __func__,nPts);

    if(NULL == hIfe)
    {
        eRet = IFE_INVALID_ARGS;
        ALOGE("%s: hIfe = Null pointer", __func__);
    }
    else
    {
        pIfe = (IfeContext*)hIfe;
        int64_t nExpectedPtsDiff;

        /* FPS is in milli Hz and Pts is in nano seconds */
        nExpectedPtsDiff = 1000 * s2ns(1) / pIfe->nCurrentFps;
        /* Ignore PTS if the same buffer is repeated */
        if(nPts == pIfe->nPrevPts)
        {
            ALOGD("%s: newPTS == oldPTS = %lld, ignoring newPTS ",
                __func__, nPts);
            pIfe->nConsecutiveFrameDrops = 0;
        }
        /* If new PTS is less than old PTS, its probably SEEK or new content */
        else if (nPts < pIfe->nPrevPts)
        {
            IfeReset((void *)pIfe);
            CalculateFps(pIfe, nPts);
            pIfe->nConsecutiveFrameDrops = 0;
        }
        /* Detect frame drop if PTS jump is noticed*/
        else if (((nPts - pIfe->nPrevPts) / nExpectedPtsDiff ) >= 2)
        {
            AddInterpolatedPts(pIfe, nPts);
            pIfe->nConsecutiveFrameDrops++;
            /* If PTS jump is repeated, its probably fps change */
            if(FPS_CHANGE_DETECT_THRESHOLD <= pIfe->nConsecutiveFrameDrops)
            {
                IfeReset((void *)pIfe);
                pIfe->nConsecutiveFrameDrops = 0;
            }
            CalculateFps(pIfe, nPts);
        }
        /* Regular case when there are no PTS discontinuities */
        else
        {
            pIfe->nConsecutiveFrameDrops = 0;
            CalculateFps(pIfe, nPts);
        }
    }
    ALOGD("%s: X", __func__);
    return eRet;
}

/*******************************************************************************
 * Function: IfeGetCurrentFps
 * Description: This returns the fps estimated as per PTS samples so far
 *
 * Input parameters:
 * hIfe      - handle to the instance of Input FPS estimator
 * pnFps     - pointer to FPS value in milli Hz
 *
 * Return values:
 * IFE_NO_ERROR      - Success without any errors
 * IFE_INVALID_ARGS  - Invalid input arguments/parameters
 *
 * Notes: none
 ******************************************************************************/
IFE_ERRORTYPE IfeGetCurrentFps(IN    void* hIfe,
                               INOUT int*  pnFps)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    IfeContext*   pIfe = NULL;

    ALOGD("%s: E", __func__);
    if(NULL == hIfe)
    {
        eRet = IFE_INVALID_ARGS;
        ALOGE("%s: hIfe = Null pointer", __func__);
    }
    else
    {
        pIfe = (IfeContext*)hIfe;
        *pnFps = pIfe->nCurrentFps;
    }
    ALOGD("%s: X", __func__);
    return eRet;
}

/*******************************************************************************
 * Function: IfeReset
 * Description: This resets all the calculations done so far and sets
 *              current FPS to DEFAULT_FPS
 *
 * Input parameters:
 * hIfe      - handle to the instance of Input FPS estimator
 *
 * Return values:
 * IFE_NO_ERROR      - Success without any errors
 * IFE_INVALID_ARGS  - Invalid input arguments/parameters
 *
 * Notes: none
 ******************************************************************************/
IFE_ERRORTYPE IfeReset(IN    void* hIfe)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    IfeContext*   pIfe = NULL;
    int           nSlidingWindowWidth;

    ALOGD("%s: E", __func__);
    if(NULL == hIfe)
    {
        eRet = IFE_INVALID_ARGS;
        ALOGE("%s: hIfe = Null pointer", __func__);
    }
    else
    {
        pIfe = (IfeContext*)hIfe;
        /* Backup sliding width config paramter */
        nSlidingWindowWidth = pIfe->nSlidingWindowWidth;
        /* All the context variables is set to default values */
        InitializeContext(pIfe);
        /* Restore sliding width config paramter */
        pIfe->nSlidingWindowWidth = nSlidingWindowWidth;
    }
    ALOGD("%s: X", __func__);
    return eRet;
}

static IFE_ERRORTYPE InitializeContext(IfeContext*   pIfe)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    ALOGD("%s: E", __func__);

    pIfe->nSlidingWindowWidth          = 0;
    pIfe->nPrevPts                     = 0;
    pIfe->nCurrentFps                  = DEFAULT_FPS;
    pIfe->nConsecutiveFrameDrops       = 0;

    pIfe->sSlidingWindow.nWindowStartIndx  = 0;
    pIfe->sSlidingWindow.nWindowEndIndx    = 0;
    pIfe->sSlidingWindow.nValidWindowWidth = 0;
    pIfe->sSlidingWindow.nSumPtsDiffs      = 0;

    ALOGD("%s: X", __func__);
    return eRet;
}

/**
* This function interpolated Pts samples based on current FPS
*/
static IFE_ERRORTYPE AddInterpolatedPts(IfeContext*   pIfe, int64_t nPts)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    int64_t       nExpectedPtsDiff = 0;
    int           nNumPtsInterpolations = 0;
    int           nLoopCount = 0;
    ALOGD("%s: E", __func__);

    /* FPS is in milli Hz and Pts is in nano seconds */
    nExpectedPtsDiff = 1000 * s2ns(1) / pIfe->nCurrentFps;

    /* 1 less than the overall PTS jump needs to be interpolated. The last */
    /* pts value is available as nPts and hence need not be interpolated */
    nNumPtsInterpolations = (nPts - pIfe->nPrevPts) / nExpectedPtsDiff - 1;

    for(nLoopCount = 0; nLoopCount < nNumPtsInterpolations; nLoopCount++)
    {
        CalculateFps(pIfe, pIfe->nPrevPts + nExpectedPtsDiff);
    }

    ALOGD("%s: X", __func__);
    return eRet;
}

/**
* This function inserts the nPts in the sliding window and calcultates
* the average of PTS differences across all the PTS samples and thus
* caculates the average FPS
*/
static IFE_ERRORTYPE CalculateFps(IfeContext*   pIfe, int64_t nPts)
{
    IFE_ERRORTYPE eRet = IFE_NO_ERROR;
    SlidingWindow *pSW = &pIfe->sSlidingWindow;

    ALOGD("%s: E", __func__);
    pSW->nPTS[pSW->nWindowEndIndx] = nPts;
    int64_t       nNewPtsDiff = 0;
    pSW->nValidWindowWidth++;
    /* PTS diff can be calculated only when atleast 2 PTS samples are available*/
    if(pSW->nValidWindowWidth > 1)
    {
        int64_t nNewPtsDiff = nPts - pIfe->nPrevPts;
        pSW->nSumPtsDiffs += nNewPtsDiff;
    }
    /* Point nWindowEndIndx to the next writable array index */
    pSW->nWindowEndIndx = (pSW->nWindowEndIndx + 1) % MAX_SLIDING_WIN_WIDTH;
    /* if active width is more than the intended sliding width, remove */
    /* oldest PTS from the window */
    if(pSW->nValidWindowWidth > pIfe->nSlidingWindowWidth)
    {
        int64_t nOldestPtsDiff = 0;
        nOldestPtsDiff =
            pSW->nPTS[(pSW->nWindowStartIndx + 1) % MAX_SLIDING_WIN_WIDTH] -
                pSW->nPTS[pSW->nWindowStartIndx];
        pSW->nSumPtsDiffs -= nOldestPtsDiff;
        /* Increment the start index of array so that older PTS is not */
        /* referred anymore */
        pSW->nWindowStartIndx =
            (pSW->nWindowStartIndx + 1) % MAX_SLIDING_WIN_WIDTH;
        pSW->nValidWindowWidth--;
    }
    /* Calculate average PTS diff and hence the fps */
    /* PTS diff can be calculated only when atleast 2 PTS samples are available*/
    if(pSW->nValidWindowWidth > 1)
    {
        /* number of PTS values = nValidWindowWidth */
        /* number of PTS differences = nValidWindowWidth - 1 */
        int64_t nAvgPtsDiff = pSW->nSumPtsDiffs / (pSW->nValidWindowWidth - 1);

        /*PTS is in nanoseconds and FPS is in milli Hz */
        pIfe->nCurrentFps = (1000 * (int64_t) s2ns(1)) / nAvgPtsDiff;
        ALOGD("%s: nCurrentFps: %d, nPts: %lld, nPrevPts: %lld, "
          "nSumPtsDiffs: %lld, nValidWindowWidth: %d nAvgPtsDiff: %lld",
          __func__, pIfe->nCurrentFps, nPts, pIfe->nPrevPts,
          pSW->nSumPtsDiffs, pSW->nValidWindowWidth, nAvgPtsDiff);
    }
    pIfe->nPrevPts = nPts;
    ALOGD("%s: X", __func__);
    return eRet;
}

