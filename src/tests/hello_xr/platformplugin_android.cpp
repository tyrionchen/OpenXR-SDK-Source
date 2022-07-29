// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pch.h"
#include "common.h"
#include "platformdata.h"
#include "platformplugin.h"

#ifdef XR_USE_PLATFORM_ANDROID

namespace {

#include <jni.h>

JNIEnv *AttachJava(JavaVM *gJavaVM)
{
    JavaVMAttachArgs args = {JNI_VERSION_1_4, 0, 0};
    JNIEnv* jni;
    int status = gJavaVM->AttachCurrentThread( &jni, &args);
    if (status < 0) {
        Log::Write(Log::Level::Info, Fmt("<SurfaceTexture> faild to attach current thread!"));
        return NULL;
    }
    Log::Write(Log::Level::Info, Fmt("xxxxxxxxx AttachJava"));
    return jni;
}

struct AndroidPlatformPlugin : public IPlatformPlugin {
    AndroidPlatformPlugin(const std::shared_ptr<Options>& /*unused*/, const std::shared_ptr<PlatformData>& data) {
        instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        instanceCreateInfoAndroid.applicationVM = data->applicationVM;
        instanceCreateInfoAndroid.applicationActivity = data->applicationActivity;
        JNIEnv *jni = AttachJava((JavaVM *)(data->applicationVM));
    }

    std::vector<std::string> GetInstanceExtensions() const override { return {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME}; }

    XrBaseInStructure* GetInstanceCreateExtension() const override { return (XrBaseInStructure*)&instanceCreateInfoAndroid; }

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid;
};
}  // namespace

std::shared_ptr<IPlatformPlugin> CreatePlatformPlugin_Android(const std::shared_ptr<Options>& options,
                                                              const std::shared_ptr<PlatformData>& data) {
    return std::make_shared<AndroidPlatformPlugin>(options, data);
}
#endif
