// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pch.h"
#include "common.h"
#include "geometry.h"
#include "graphicsplugin.h"
#include "options.h"

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES

#include "common/gfxwrapper_opengl.h"
#include <common/xr_linear.h>
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaExtractor.h"

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

namespace {

// The version statement has come on first line.
static const char* VertexShaderGlsl = R"_(#version 320 es

    in vec3 VertexPos;
    in vec3 VertexColor;

    out vec3 PSVertexColor;

    uniform mat4 ModelViewProjection;

    void main() {
       gl_Position = ModelViewProjection * vec4(VertexPos, 1.0);
       PSVertexColor = VertexColor;
    }
    )_";

// The version statement has come on first line.
static const char* FragmentShaderGlsl = R"_(#version 320 es

    in lowp vec3 PSVertexColor;
    out lowp vec4 FragColor;

    void main() {
       FragColor = vec4(PSVertexColor, 1);
    }
    )_";

struct OpenGLESGraphicsPlugin : public IGraphicsPlugin {
    OpenGLESGraphicsPlugin(const std::shared_ptr<Options>& options, const std::shared_ptr<IPlatformPlugin> /*unused*/&)
        : m_clearColor(options->GetBackgroundClearColor()) {}

    OpenGLESGraphicsPlugin(const OpenGLESGraphicsPlugin&) = delete;
    OpenGLESGraphicsPlugin& operator=(const OpenGLESGraphicsPlugin&) = delete;
    OpenGLESGraphicsPlugin(OpenGLESGraphicsPlugin&&) = delete;
    OpenGLESGraphicsPlugin& operator=(OpenGLESGraphicsPlugin&&) = delete;

    ~OpenGLESGraphicsPlugin() override {
        if (m_swapchainFramebuffer != 0) {
            glDeleteFramebuffers(1, &m_swapchainFramebuffer);
        }
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
        }
        if (m_cubeVertexBuffer != 0) {
            glDeleteBuffers(1, &m_cubeVertexBuffer);
        }
        if (m_cubeIndexBuffer != 0) {
            glDeleteBuffers(1, &m_cubeIndexBuffer);
        }

        for (auto& colorToDepth : m_colorToDepthMap) {
            if (colorToDepth.second != 0) {
                glDeleteTextures(1, &colorToDepth.second);
            }
        }

        ksGpuWindow_Destroy(&window);
    }

    std::vector<std::string> GetInstanceExtensions() const override { return {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME}; }

    ksGpuWindow window{};

    void DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message) {
        (void)source;
        (void)type;
        (void)id;
        (void)severity;
        Log::Write(Log::Level::Info, "GLES Debug: " + std::string(message, 0, length));
    }


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

    jweak mesosClassLoader = NULL; // Initialized in JNI_OnLoad later in this file.

    jclass FindMesosClass(JNIEnv* env, const char* className)
    {
        if (env->ExceptionCheck()) {
            fprintf(stderr, "ERROR: exception pending on entry to "
                            "FindMesosClass()\n");
            return NULL;
        }

        if (mesosClassLoader == NULL) {
            return env->FindClass(className);
        }

        // JNI FindClass uses class names with slashes, but
        // ClassLoader.loadClass uses the dotted "binary name"
        // format. Convert formats.
        std::string convName = className;
        for (int i = 0; i < convName.size(); i++) {
            if (convName[i] == '/')
                convName[i] = '.';
        }

        jclass javaLangClassLoader = env->FindClass("java/lang/ClassLoader");
        assert(javaLangClassLoader != NULL);
        jmethodID loadClass =
                env->GetMethodID(javaLangClassLoader,
                                 "loadClass",
                                 "(Ljava/lang/String;)Ljava/lang/Class;");
        assert(loadClass != NULL);

        // Create an object for the class name string; alloc could fail.
        jstring strClassName = env->NewStringUTF(convName.c_str());
        if (env->ExceptionCheck()) {
            fprintf(stderr, "ERROR: unable to convert '%s' to string\n",
                    convName.c_str());
            return NULL;
        }

        // Try to find the named class.
        jclass cls = (jclass) env->CallObjectMethod(mesosClassLoader,
                                                    loadClass,
                                                    strClassName);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            fprintf(stderr, "ERROR: unable to load class '%s' from %p\n",
                    className, mesosClassLoader);
            return NULL;
        }

        return cls;
    }


    jclass retrieveClass(JNIEnv *jni, ANativeActivity* activity,
                         const char* className) {
        jclass activityClass = jni->FindClass("android/app/NativeActivity");
        jmethodID getClassLoader = jni->GetMethodID(activityClass, "getClassLoader",
                                                    "()Ljava/lang/ClassLoader;");
        jobject cls = jni->CallObjectMethod(activity->clazz, getClassLoader);
        jclass classLoader = jni->FindClass("java/lang/ClassLoader");
        jmethodID findClass = jni->GetMethodID(classLoader, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring strClassName = jni->NewStringUTF(className);
        jclass classRetrieved = (jclass) jni->CallObjectMethod(cls, findClass,
                                                               strClassName);
        jni->DeleteLocalRef(strClassName);
        return classRetrieved;
    }
    
    void SetXrBaseInStructure(XrBaseInStructure* baseInStructure) override {
        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure baseInStructure:%p", baseInStructure));

        JavaVM *gJavaVM = ((JavaVM *)((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationVM);
        m_context = ((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationActivity;
        
        m_jni = AttachJava(gJavaVM);

//        m_jni->FindClass("java/lang/Thread");
//        
//        // Find thread's context class loader.
//        jclass javaLangThread = m_jni->FindClass("java/lang/Thread");
//        assert(javaLangThread != NULL);
//
//        jclass javaLangClassLoader = m_jni->FindClass("java/lang/ClassLoader");
//        assert(javaLangClassLoader != NULL);
//
//        jmethodID currentThread = m_jni->GetStaticMethodID(
//                javaLangThread, "currentThread", "()Ljava/lang/Thread;");
//        assert(currentThread != NULL);
//
//        jmethodID getContextClassLoader = m_jni->GetMethodID(
//                javaLangThread, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
//        assert(getContextClassLoader != NULL);
//
//        jobject thread = m_jni->CallStaticObjectMethod(javaLangThread, currentThread);
//        assert(thread != NULL);
//
//        jobject classLoader = m_jni->CallObjectMethod(thread, getContextClassLoader);
//
//        if (classLoader != NULL) {
//            mesosClassLoader = m_jni->NewWeakGlobalRef(classLoader);
//        }

//        jclass context_wrapper_clz = m_jni->FindClass("android/content/ContextWrapper");
//        jmethodID getContextClassLoader = m_jni->GetMethodID(context_wrapper_clz, "getClassLoader", "()Ljava/lang/ClassLoader;");
//        jobject classloader = m_jni->CallObjectMethod((jobject)m_context, getContextClassLoader);
//        mesosClassLoader = m_jni->NewWeakGlobalRef(classloader);


//        jclass media_texture_clz = FindMesosClass(m_jni, "com/khronos/hello_xr/MediaTexture");
//        
//        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure classloader:%p media_texture_clz:%p", classloader, media_texture_clz));
        
        // Check that we are loading the correct version of the native library.
//        jclass media_texture_clz = retrieveClass(m_jni, (ANativeActivity*)m_context, "com/khronos/hello_xr/MediaTexture");
//        
//        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure 2 m_context:%p media_texture_clz:%p", m_context, media_texture_clz));

//        jclass media_texture_clz = m_jni->FindClass("com/khronos/hello_xr/MediaTexture");
//        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure media_texture_clz:%p", media_texture_clz));
        
//        jclass activity_clz = m_jni->FindClass("android/view/ContextThemeWrapper");
//        jmethodID activity_get_resource_method = m_jni->GetMethodID(activity_clz, "getResources", "()Landroid/content/res/Resources;");
//        jobject resource_obj = m_jni->CallObjectMethod((jobject)((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationActivity, activity_get_resource_method);
//        jclass resource_clz = m_jni->FindClass("android/content/res/Resources");
//        jmethodID resource_get_assert_method = m_jni->GetMethodID(resource_clz, "getAssets", "()Landroid/content/res/AssetManager;");
//        jobject asset_mgr_obj = m_jni->CallObjectMethod(resource_obj, resource_get_assert_method);
//
//
//        const char* filename = "demo_video.mp4";
//
//        off_t outStart, outLen;
//        int fd = AAsset_openFileDescriptor(AAssetManager_open(AAssetManager_fromJava(m_jni, asset_mgr_obj), filename, 0),
//                                           &outStart, &outLen);
//        if (fd < 0) {
//            Log::Write(Log::Level::Error, Fmt("failed to open file: %s %d (%s)", filename, fd, strerror(errno)));
//            return;
//        }
        
//        wd.fd = fd;
//        wd.outStart = outStart;
//        wd.outLen = outLen;
    }
  
    void InitializeDevice(XrInstance instance, XrSystemId systemId) override {
        // Extension function must be loaded by name
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                          reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLESGraphicsRequirementsKHR)));

        XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        CHECK_XRCMD(pfnGetOpenGLESGraphicsRequirementsKHR(instance, systemId, &graphicsRequirements));

        // Initialize the gl extensions. Note we have to open a window.
        ksDriverInstance driverInstance{};
        ksGpuQueueInfo queueInfo{};
        ksGpuSurfaceColorFormat colorFormat{KS_GPU_SURFACE_COLOR_FORMAT_B8G8R8A8};
        ksGpuSurfaceDepthFormat depthFormat{KS_GPU_SURFACE_DEPTH_FORMAT_D24};
        ksGpuSampleCount sampleCount{KS_GPU_SAMPLE_COUNT_1};
        if (!ksGpuWindow_Create(&window, &driverInstance, &queueInfo, 0, colorFormat, depthFormat, sampleCount, 640, 480, false)) {
            THROW("Unable to create GL context");
        }

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);
        if (graphicsRequirements.minApiVersionSupported > desiredApiVersion) {
            THROW("Runtime does not support desired Graphics API and/or version");
        }

        m_contextApiMajorVersion = major;

#if defined(XR_USE_PLATFORM_ANDROID)
        m_graphicsBinding.display = window.display;
        m_graphicsBinding.config = (EGLConfig)0;
        m_graphicsBinding.context = window.context.context;
#endif

        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(
            [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message,
               const void* userParam) {
                ((OpenGLESGraphicsPlugin*)userParam)->DebugMessageCallback(source, type, id, severity, length, message);
            },
            this);

        InitializeResources();
//        InitializeMedia();
        InitMediaTexture(0);
    }
    
    void InitMediaTexture(int textureId) {
        UNUSED(textureId);
        
        
//        jclass media_texture_clz = m_jni->FindClass("com/khronos/hello_xr/MediaTexture");
//        Log::Write(Log::Level::Info, Fmt("InitMediaTexture m_context:%p media_texture_clz:%p", m_context, media_texture_clz));
//        jmethodID media_texture_constructor = m_jni->GetMethodID(media_texture_clz, "<init>", "(ILandroid/content/Context;)V");
//        m_mediaTexture = m_jni->NewObject(media_texture_clz, media_texture_constructor, textureId, (jobject)m_context);
//        m_media_texture_update_method = m_jni->GetMethodID(media_texture_clz, "updateTexture", "([F)V");
//
//        m_texture_matrix = m_jni->NewFloatArray(16);
    }
    
    void updateTexture() {
        m_jni->CallVoidMethod(m_mediaTexture, m_media_texture_update_method, m_texture_matrix);
        jboolean val;
        jfloat* matrix = m_jni->GetFloatArrayElements(m_texture_matrix, &val);
    }

    typedef struct {
        int fd;
        int outStart;
        int outLen;
        
        AMediaExtractor* ex;
        AMediaCodec *codec;
        int64_t renderStart;
        bool sawInputEOS;
        bool sawOutputEOS;
        bool isPlaying;
        bool renderOnce;
    } workerdata;

    workerdata wd = {-1, -1, -1, nullptr, nullptr, 0, false, false, false, false};


    void InitializeMedia() {
        if (wd.fd < 0) {
            Log::Write(Log::Level::Error, Fmt("InitializeMedia-> failed wd.fd:%d", wd.fd));
            return;
        }
        
        workerdata *d = &wd;
        
        AMediaExtractor *ex = AMediaExtractor_new();
        media_status_t err = AMediaExtractor_setDataSourceFd(ex, d->fd,
                                                             static_cast<off64_t>(d->outStart),
                                                             static_cast<off64_t>(d->outLen));
        close(d->fd);
        if (err != AMEDIA_OK) {
            Log::Write(Log::Level::Error, Fmt("InitializeMedia-> setDataSource error: %d", err));
            return;
        }

        int numtracks = AMediaExtractor_getTrackCount(ex);

        AMediaCodec *codec = nullptr;

        Log::Write(Log::Level::Info, Fmt("InitializeMedia->input has %d tracks", numtracks));
        for (int i = 0; i < numtracks; i++) {
            AMediaFormat *format = AMediaExtractor_getTrackFormat(ex, i);
            const char *s = AMediaFormat_toString(format);
            Log::Write(Log::Level::Info, Fmt("InitializeMedia->track %d format: %s", i, s));
            const char *mime;
            if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
                Log::Write(Log::Level::Error, Fmt("InitializeMedia->no mime type"));
                return ;
            } else if (!strncmp(mime, "video/", 6)) {
                // Omitting most error handling for clarity.
                // Production code should check for errors.

                // Subsequent calls to readSampleData(ByteBuffer, int), getSampleTrackIndex() and getSampleTime() only retrieve information for the subset of tracks selected.
                // Selecting the same track multiple times has no effect, the track is only selected once.
                AMediaExtractor_selectTrack(ex, i);
                codec = AMediaCodec_createDecoderByType(mime);

                // 第三个参数传入null表示不直接渲染上屏
                AMediaCodec_configure(codec, format, nullptr, nullptr, 0);
                d->ex = ex;
                d->codec = codec;
                d->renderStart = -1;
                d->sawInputEOS = false;
                d->sawOutputEOS = false;
                d->isPlaying = false;
                d->renderOnce = true;
                // 开始解码
                AMediaCodec_start(codec);
            }
            // 配置完需要把format还回去
            AMediaFormat_delete(format);
        }
    }
    
    void InitializeResources() {
        // 屏幕缓冲有以下几种:
        // 1.用于写入颜色值的 颜色缓冲
        // 2.用于写入深度信息的 深度缓冲
        // 3.允许基于一些条件丢弃指定片段的 模板缓冲
        // 把这三种缓冲类型结合起来交帧缓冲Framebuffer
        //
        // 默认的渲染操作都在默认Framebuffer上
        //
        // 新创建的Framebuffer需要添加一些附件(Attachment)并且把它们添加到Framebuffer上
        
        // 创建一个帧缓冲对象
        glGenFramebuffers(1, &m_swapchainFramebuffer);

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &VertexShaderGlsl, nullptr);
        glCompileShader(vertexShader);
        CheckShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &FragmentShaderGlsl, nullptr);
        glCompileShader(fragmentShader);
        CheckShader(fragmentShader);

        m_program = glCreateProgram();
        glAttachShader(m_program, vertexShader);
        glAttachShader(m_program, fragmentShader);
        glLinkProgram(m_program);
        CheckProgram(m_program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        m_modelViewProjectionUniformLocation = glGetUniformLocation(m_program, "ModelViewProjection");

        m_vertexAttribCoords = glGetAttribLocation(m_program, "VertexPos");
        m_vertexAttribColor = glGetAttribLocation(m_program, "VertexColor");

        glGenBuffers(1, &m_cubeVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_cubeVertices), Geometry::c_cubeVertices, GL_STATIC_DRAW);

        glGenBuffers(1, &m_cubeIndexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_cubeIndices), Geometry::c_cubeIndices, GL_STATIC_DRAW);

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glEnableVertexAttribArray(m_vertexAttribCoords);
        glEnableVertexAttribArray(m_vertexAttribColor);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndexBuffer);
        glVertexAttribPointer(m_vertexAttribCoords, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex), nullptr);
        glVertexAttribPointer(m_vertexAttribColor, 3, GL_FLOAT, GL_FALSE, sizeof(Geometry::Vertex),
                              reinterpret_cast<const void*>(sizeof(XrVector3f)));
    }

    void CheckShader(GLuint shader) {
        GLint r = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096] = {};
            GLsizei length;
            glGetShaderInfoLog(shader, sizeof(msg), &length, msg);
            THROW(Fmt("Compile shader failed: %s", msg));
        }
    }

    void CheckProgram(GLuint prog) {
        GLint r = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &r);
        if (r == GL_FALSE) {
            GLchar msg[4096] = {};
            GLsizei length;
            glGetProgramInfoLog(prog, sizeof(msg), &length, msg);
            THROW(Fmt("Link program failed: %s", msg));
        }
    }

    int64_t SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) const override {
        // List of supported color swapchain formats.
        std::vector<int64_t> supportedColorSwapchainFormats{GL_RGBA8, GL_RGBA8_SNORM};

        // In OpenGLES 3.0+, the R, G, and B values after blending are converted into the non-linear
        // sRGB automatically.
        if (m_contextApiMajorVersion >= 3) {
            supportedColorSwapchainFormats.push_back(GL_SRGB8_ALPHA8);
        }

        auto swapchainFormatIt = std::find_first_of(runtimeFormats.begin(), runtimeFormats.end(),
                                                    supportedColorSwapchainFormats.begin(), supportedColorSwapchainFormats.end());
        if (swapchainFormatIt == runtimeFormats.end()) {
            THROW("No runtime swapchain format supported for color swapchain");
        }

        return *swapchainFormatIt;
    }

    const XrBaseInStructure* GetGraphicsBinding() const override {
        return reinterpret_cast<const XrBaseInStructure*>(&m_graphicsBinding);
    }

    std::vector<XrSwapchainImageBaseHeader*> AllocateSwapchainImageStructs(
        uint32_t capacity, const XrSwapchainCreateInfo& /*swapchainCreateInfo*/) override {
        // Allocate and initialize the buffer of image structs (must be sequential in memory for xrEnumerateSwapchainImages).
        // Return back an array of pointers to each swapchain image struct so the consumer doesn't need to know the type/size.
        std::vector<XrSwapchainImageOpenGLESKHR> swapchainImageBuffer(capacity);
        std::vector<XrSwapchainImageBaseHeader*> swapchainImageBase;
        for (XrSwapchainImageOpenGLESKHR& image : swapchainImageBuffer) {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            swapchainImageBase.push_back(reinterpret_cast<XrSwapchainImageBaseHeader*>(&image));
        }

        // Keep the buffer alive by moving it into the list of buffers.
        m_swapchainImageBuffers.push_back(std::move(swapchainImageBuffer));

        return swapchainImageBase;
    }

    uint32_t GetDepthTexture(uint32_t colorTexture) {
        // If a depth-stencil view has already been created for this back-buffer, use it.
        auto depthBufferIt = m_colorToDepthMap.find(colorTexture);
        if (depthBufferIt != m_colorToDepthMap.end()) {
            return depthBufferIt->second;
        }

        // This back-buffer has no corresponding depth-stencil texture, so create one with matching dimensions.

        GLint width;
        GLint height;
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

        uint32_t depthTexture;
        glGenTextures(1, &depthTexture);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        m_colorToDepthMap.insert(std::make_pair(colorTexture, depthTexture));

        return depthTexture;
    }

  int64_t systemnanotime() {
      timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      return now.tv_sec * 1000000000LL + now.tv_nsec;
  }

  void nv212Yv12(char *nv21, char *yv12, int width, int height)
  {
    int frameSize = width * height;
    memcpy(yv12, nv21, frameSize);
    nv21 += frameSize;
    yv12 += frameSize;
    int halfWidth = width / 2;
    int halfHeight = height / 2;
    int quadFrame = halfWidth * halfHeight;
    for (int i = 0; i < halfHeight; i++) {
      for (int j = 0; j < halfWidth; j++) {
        *(yv12 + i * halfWidth + j) = *nv21++;
        *(yv12 + quadFrame + i * halfWidth + j) = *nv21++;
      }
    }
  }

  int width = 1440;
  int height = 720;
  int sliceHeight = 720;
  int stride = 1440;
  
  char* doCodecWork(workerdata *d) {
      ssize_t bufidx = -1;
      if (!d->sawInputEOS) {
          // 获取可用input buffer索引
          bufidx = AMediaCodec_dequeueInputBuffer(d->codec, 2000);
          Log::Write(Log::Level::Error, Fmt("InitializeMedia->doCodecWork input buffer %zd", bufidx));
          if (bufidx >= 0) {
              size_t bufsize;
              // 获取input buffer
              auto buf = AMediaCodec_getInputBuffer(d->codec, bufidx, &bufsize);
              // 读取未解码数据到input buffer
              auto sampleSize = AMediaExtractor_readSampleData(d->ex, buf, bufsize);
              // sampleSize小于0表示未解码数据读完了
              if (sampleSize < 0) {
                  sampleSize = 0;
                  d->sawInputEOS = true;
                  Log::Write(Log::Level::Error, "InitializeMedia->doCodecWork EOS");
              }
              // Returns the current sample's presentation time in microseconds
              auto presentationTimeUs = AMediaExtractor_getSampleTime(d->ex);

              // 塞一个buffer进去解码
              AMediaCodec_queueInputBuffer(d->codec, bufidx, 0, sampleSize, presentationTimeUs,
                                           d->sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
              AMediaExtractor_advance(d->ex);
          }
      }

      if (!d->sawOutputEOS) {
          AMediaCodecBufferInfo info;
          // 取解码后的数据
          auto status = AMediaCodec_dequeueOutputBuffer(d->codec, &info, 0);
          if (status >= 0) {
              if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                  Log::Write(Log::Level::Error, "InitializeMedia->doCodecWork output EOS");
                  d->sawOutputEOS = true;
              }
              int64_t presentationNano = info.presentationTimeUs * 1000;
              if (d->renderOnce < 0) {
                  d->renderOnce = systemnanotime() - presentationNano;
              }
              int64_t delay = (d->renderStart + presentationNano) - systemnanotime();
              if (delay > 0) {
                  usleep(delay / 1000);
              }
              

              size_t bufSize;
              auto buf = AMediaCodec_getOutputBuffer(d->codec, status, &bufSize);
              char* yuv = nullptr;
              if (buf != nullptr) {
                 size_t sizeYUV = width * height * 3 / 2;
                 yuv = new char[sizeYUV + 4 * 3];
                 *(int *)yuv = width;
                 *(int *)(yuv + 4) = height;
                 *(int *)(yuv + 8) = sizeYUV;
                 
                 char *dst = yuv + 4 * 3;
                 uint8_t *src = buf + info.offset;
                 memcpy(dst, src, width * height);
                 src += sliceHeight * stride;
                 dst += width * height;
                 memcpy(dst, src, width * height / 2);

                 char *yuvYv12 = new char[sizeYUV + 4 * 3];
                 Log::Write(Log::Level::Info, Fmt("InitializeMedia->doCodecWork xxxxx sizeYUV:%zd sizeof:%zd", sizeYUV, sizeof(yuvYv12)));
                 *(int *)yuvYv12 = width;
                 *(int *)(yuvYv12 + 4) = height;
                 *(int *)(yuvYv12 + 8) = sizeYUV;
                 nv212Yv12(yuv + 4 * 3, yuvYv12 + 4 * 3, width, height);
                 delete[] yuv;
                 yuv = yuvYv12;
                 // yuv就是我们的数据
              }
              
              Log::Write(Log::Level::Info, Fmt("InitializeMedia->doCodecWork buf size:%zd", bufSize));
              // 释放buffer， 由于前面配置了Surface，所以这里会直接渲染
              AMediaCodec_releaseOutputBuffer(d->codec, status, info.size != 0);
              if (d->renderOnce) {
                  d->renderOnce = false;
              }
              return yuv;
          } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
              Log::Write(Log::Level::Error, "InitializeMedia->doCodecWork output buffers changed");
          } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
              auto format = AMediaCodec_getOutputFormat(d->codec);
              Log::Write(Log::Level::Error, Fmt("InitializeMedia->doCodecWork format changed to: %s", AMediaFormat_toString(format)));
              // mime: string(video/raw), 
              // stride: int32(1440), 
              // slice-height: int32(720),
              // color-format: int32(21), 
              // image-data: data, 
              // crop: Rect(0, 0, 1439, 719), 
              // hdr-static-info: data, width: 
              // int32(1440), height: int32(720)}
              AMediaFormat_delete(format);

              // color-format的取值: MediaCodecInfo.COLOR_FormatYUV420SemiPlanar等
            
          } else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
              Log::Write(Log::Level::Error, "InitializeMedia->doCodecWork no output buffer right now");
          } else {
              Log::Write(Log::Level::Error, Fmt("InitializeMedia->doCodecWork unexpected info code: %zd", status));
          }
      }
      return nullptr;
  }

  
  void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage,
                    int64_t swapchainFormat, const std::vector<Cube>& cubes) override {
        CHECK(layerView.subImage.imageArrayIndex == 0);  // Texture arrays not supported.
        UNUSED_PARM(swapchainFormat);                    // Not used in this function for now.

//        char* yuvData = nullptr;
//        if (!wd.sawInputEOS || !wd.sawOutputEOS) {
//             // 应该是播完了
//           yuvData = doCodecWork(&wd);
//           if (yuvData != nullptr) {
//              Log::Write(Log::Level::Error, Fmt("InitializeMedia->RenderView yuvData:%p len:%zd", yuvData,
//                                                sizeof(yuvData)));
//
//              std::string str; 
//              for (int i = 0; i < sizeof yuvData; i++) {
//                  char tmp[3] = {0};
//                  sprintf(tmp, "%02x", yuvData[i]);
//                  str.append(tmp);
//              }
//             Log::Write(Log::Level::Error, Fmt("InitializeMedia->RenderView str:%s", str.c_str()));
//            }
//        }

        
        glBindFramebuffer(GL_FRAMEBUFFER, m_swapchainFramebuffer);

        const uint32_t colorTexture = reinterpret_cast<const XrSwapchainImageOpenGLESKHR*>(swapchainImage)->image;

        glViewport(static_cast<GLint>(layerView.subImage.imageRect.offset.x),
                   static_cast<GLint>(layerView.subImage.imageRect.offset.y),
                   static_cast<GLsizei>(layerView.subImage.imageRect.extent.width),
                   static_cast<GLsizei>(layerView.subImage.imageRect.extent.height));

        glFrontFace(GL_CW);
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        const uint32_t depthTexture = GetDepthTexture(colorTexture);

        // 当把纹理
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

        // Clear swapchain and depth buffer.
        glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // Set shaders and uniform variables.
        glUseProgram(m_program);

        const auto& pose = layerView.pose;
        XrMatrix4x4f proj;
        XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_OPENGL_ES, layerView.fov, 0.05f, 100.0f);
        XrMatrix4x4f toView;
        XrVector3f scale{1.f, 1.f, 1.f};
        XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);
        XrMatrix4x4f view;
        XrMatrix4x4f_InvertRigidBody(&view, &toView);
        XrMatrix4x4f vp;
        XrMatrix4x4f_Multiply(&vp, &proj, &view);

        // Set cube primitive data.
        glBindVertexArray(m_vao);

        // Render each cube
        for (const Cube& cube : cubes) {
            // Compute the model-view-projection transform and set it..
            XrMatrix4x4f model;
            XrMatrix4x4f_CreateTranslationRotationScale(&model, &cube.Pose.position, &cube.Pose.orientation, &cube.Scale);
            XrMatrix4x4f mvp;
            XrMatrix4x4f_Multiply(&mvp, &vp, &model);
            glUniformMatrix4fv(m_modelViewProjectionUniformLocation, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(&mvp));

            // Draw the cube.
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_cubeIndices)), GL_UNSIGNED_SHORT, nullptr);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    uint32_t GetSupportedSwapchainSampleCount(const XrViewConfigurationView&) override { return 1; }

   private:
#ifdef XR_USE_PLATFORM_ANDROID
    XrGraphicsBindingOpenGLESAndroidKHR m_graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
#endif

    std::list<std::vector<XrSwapchainImageOpenGLESKHR>> m_swapchainImageBuffers;
    GLuint m_swapchainFramebuffer{0};
    GLuint m_program{0};
    GLint m_modelViewProjectionUniformLocation{0};
    GLint m_vertexAttribCoords{0};
    GLint m_vertexAttribColor{0};
    GLuint m_vao{0};
    GLuint m_cubeVertexBuffer{0};
    GLuint m_cubeIndexBuffer{0};
    GLint m_contextApiMajorVersion{0};

    // Map color buffer to associated depth buffer. This map is populated on demand.
    std::map<uint32_t, uint32_t> m_colorToDepthMap;
    const std::array<float, 4> m_clearColor;

    JNIEnv *m_jni;
    void* m_context;
    jobject m_mediaTexture;
    jfloatArray m_texture_matrix;
    jmethodID m_media_texture_update_method;
};
}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_OpenGLES(const std::shared_ptr<Options>& options,
                                                               std::shared_ptr<IPlatformPlugin> platformPlugin) {
    return std::make_shared<OpenGLESGraphicsPlugin>(options, platformPlugin);
}

#endif
