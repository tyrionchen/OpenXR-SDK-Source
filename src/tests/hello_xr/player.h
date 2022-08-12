//
// Created by cyy on 2022/8/11.
//

#ifndef HELLO_XR_SPHERE_PLAYER_H
#define HELLO_XR_SPHERE_PLAYER_H

#include "pch.h"
#include "common.h"
struct IPlayer {
  virtual ~IPlayer() = default;
  virtual void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage) = 0;
  virtual void InitializeResources() = 0;
  
  JNIEnv *AttachJava(JavaVM *gJavaVM)
  {
    JavaVMAttachArgs args = {JNI_VERSION_1_4, 0, 0};
    JNIEnv* jni;
    int status = gJavaVM->AttachCurrentThread( &jni, &args);
    if (status < 0) {
      Log::Write(Log::Level::Info, Fmt("<SurfaceTexture> faild to attach current thread!"));
      return nullptr;
    }
    Log::Write(Log::Level::Info, Fmt("xxxxxxxxx AttachJava"));
    return jni;
  }
  
  void SetXrBaseInStructure(XrBaseInStructure* baseInStructure) {
    Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure baseInStructure:%p", baseInStructure));

    JavaVM *gJavaVM = ((JavaVM *)((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationVM);
    m_context = ((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationActivity;
    m_jni = AttachJava(gJavaVM);
  }
  
  virtual void SetPos(XrPosef* pose) {
    Log::Write(Log::Level::Info, Fmt("player SetPos:%p", pose));
  }
  

  JNIEnv *m_jni;
  void* m_context;

  jobject m_mediaTexture;
  jfloatArray m_texture_matrix;
  jmethodID m_media_texture_update_method;
};

std::shared_ptr<IPlayer> createPlayer(const std::array<float, 4> color);

#endif //HELLO_XR_SPHERE_PLAYER_H
