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
--------------------------------------------------------------------------*/

#ifndef IN_FPS_ESTIMATOR_H
#define IN_FPS_ESTIMATOR_H

/** Macros defining the type of function arguments */
#define IN
#define OUT
#define INOUT

/** Macro constants */
/** Consective frame drops which is detected as FPS change */
#define FPS_CHANGE_DETECT_THRESHOLD (5)

/** Maximum width of the sliding window */
#define MAX_SLIDING_WIN_WIDTH (5000)

/** Default fps in milliHz when number of PTS samples added is less than 2 */
#define DEFAULT_FPS (24000)

/** The list of errors used by this module */
typedef enum {
    /** No Error */
    IFE_NO_ERROR                 = 0x0,

    /** General error without any specific error information */
    IFE_GENERAL_ERROR            = 0x1,

    /** Invalid arguments are passed to the function */
    IFE_INVALID_ARGS             = 0x2,

    /** Memory errors while allocating or freeing heap memory */
    IFE_MEMORY_ERROR             = 0x3,

    /** Last error in this list */
    IFE_MAX_ERROR                = 0xffff
}IFE_ERRORTYPE;

/** Create parameters structure */
typedef struct {
    /** Sliding window width in terms of number of PTS samples */
    /** Less than or equal to MAX_SLIDING_WIN_WIDTH */
    int    nSlidingWindowWidth;
}IfeCreateParams;

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
IFE_ERRORTYPE IfeCreate(IN    IfeCreateParams* ifeCrtPrms,
                        INOUT void** hIfe);

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
IFE_ERRORTYPE IfeDestroy(IN void* hIfe);

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
                        IN int64_t nPts);

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
                               INOUT int*  pnFps);

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
IFE_ERRORTYPE IfeReset(IN    void* hIfe);

#endif /* IN_FPS_ESTIMATOR_H */

