LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libbinder \
    libcutils

LOCAL_WHOLE_STATIC_LIBRARIES := libdrmutility libdrmframeworkcommon

LOCAL_MODULE := libdrmutilitywrapper

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
