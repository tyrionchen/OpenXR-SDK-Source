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

#include <jni.h>
#include "android_common.h"

namespace {

// The version statement has come on first line.
static const char* VertexShaderGlsl = R"_(#version 320 es
    in vec3 aPosition;
    in vec3 aTexCoord;

    out vec3 vTexCoord;

    void main() {
       vTexCoord = aTexCoord;
       gl_Position = vec4(aPosition, 1.0);
    }
    )_";

// The version statement has come on first line.
static const char* FragmentShaderGlsl = R"_(#version 320 es
    #extension GL_OES_EGL_image_external_essl3 : require
    precision mediump float;

    in vec3 vTexCoord;
    out vec4 FragColor;

    uniform samplerExternalOES sTexture;

    void main() {
       FragColor=texture(sTexture, vTexCoord.xy);
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

  jclass loadClz(const char* cStrClzName) {
      jobject nativeActivity = g_activity->clazz;
      jclass acl = m_jni->GetObjectClass(nativeActivity);
      jmethodID getClassLoader = m_jni->GetMethodID(acl, "getClassLoader", "()Ljava/lang/ClassLoader;");
      jobject cls = m_jni->CallObjectMethod(nativeActivity, getClassLoader);
      jclass classLoader = m_jni->FindClass("java/lang/ClassLoader");
      jmethodID findClass = m_jni->GetMethodID(classLoader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
      jstring strClassName = m_jni->NewStringUTF(cStrClzName);
      jclass media_texture_clz = (jclass)(m_jni->CallObjectMethod(cls, findClass, strClassName));
      m_jni->DeleteLocalRef(strClassName);
      return media_texture_clz;
    }

  void SetXrBaseInStructure(XrBaseInStructure* baseInStructure) override {
        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure baseInStructure:%p", baseInStructure));

        JavaVM *gJavaVM = ((JavaVM *)((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationVM);
        m_context = ((XrInstanceCreateInfoAndroidKHR*)baseInStructure)->applicationActivity;
        m_jni = AttachJava(gJavaVM);
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
    }
    
    void InitMediaTexture(int textureId) {
        jclass media_texture_clz = loadClz("com/khronos/hello_xr/MediaTexture");
        Log::Write(Log::Level::Info, Fmt("InitMediaTexture m_context:%p media_texture_clz:%p", m_context, media_texture_clz));
        jmethodID media_texture_constructor = m_jni->GetMethodID(media_texture_clz, "<init>", "(ILandroid/content/Context;)V");
        m_mediaTexture = m_jni->NewObject(media_texture_clz, media_texture_constructor, textureId, (jobject)m_context);
        m_media_texture_update_method = m_jni->GetMethodID(media_texture_clz, "updateTexture", "([F)V");
        m_texture_matrix = m_jni->NewFloatArray(16);
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
        // 顶点着色器
        Log::Write(Log::Level::Info, Fmt("InitializeResources vertexShader"));
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &VertexShaderGlsl, nullptr);
        glCompileShader(vertexShader);
        
        CheckShader(vertexShader);

        // 像素着色器
        Log::Write(Log::Level::Info, Fmt("InitializeResources fragmentShader"));
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

        // 顶点坐标
        m_vertexPosition = glGetAttribLocation(m_program, "aPosition");
        // 纹理坐标
        m_texturePosition = glGetAttribLocation(m_program, "aTexCoord");
        // 纹理采样器
        m_texture_sampler = glGetUniformLocation(m_program, "sTexture");

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        
        // 顶点数据
        glGenBuffers(1, &m_rectVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_rectVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Geometry::c_2dPlayer_vertices), Geometry::c_2dPlayer_vertices, GL_STATIC_DRAW);

        // 索引数据
        glGenBuffers(1, &m_rectIndecesBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rectIndecesBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Geometry::c_2dPlayerIndices), Geometry::c_2dPlayerIndices, GL_STATIC_DRAW);

        // 指定顶点属性
        glEnableVertexAttribArray(m_vertexPosition);
        glEnableVertexAttribArray(m_texturePosition);
        glVertexAttribPointer(m_vertexPosition, 3, GL_FLOAT, GL_FALSE, sizeof(XrVector3f)*2, nullptr);
        glVertexAttribPointer(m_texturePosition, 3, GL_FLOAT, GL_FALSE, sizeof(XrVector3f)*2, reinterpret_cast<const void*>(sizeof(XrVector3f)));
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


  int64_t systemnanotime() {
      timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      return now.tv_sec * 1000000000LL + now.tv_nsec;
  }

  void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage,
                    int64_t swapchainFormat, const std::vector<Cube>& cubes) override {
        CHECK(layerView.subImage.imageArrayIndex == 0);  // Texture arrays not supported.
        UNUSED_PARM(swapchainFormat);                    // Not used in this function for now.
        UNUSED(cubes);
    
        glBindFramebuffer(GL_FRAMEBUFFER, m_swapchainFramebuffer);

        const uint32_t colorTexture = reinterpret_cast<const XrSwapchainImageOpenGLESKHR*>(swapchainImage)->image;
        glViewport(static_cast<GLint>(layerView.subImage.imageRect.offset.x),
                   static_cast<GLint>(layerView.subImage.imageRect.offset.y),
                   static_cast<GLsizei>(layerView.subImage.imageRect.extent.width),
                   static_cast<GLsizei>(layerView.subImage.imageRect.extent.height));
//        Log::Write(Log::Level::Info, Fmt("InitializeMedia->RenderView viewport(%d,%d)[%d,%d]"
//              ,layerView.subImage.imageRect.offset.x
//              ,layerView.subImage.imageRect.offset.y,
//                                     layerView.subImage.imageRect.extent.width,
//                                     layerView.subImage.imageRect.extent.height));
    
//        glFrontFace(GL_CW);
//        glCullFace(GL_BACK);
//        glEnable(GL_CULL_FACE);
//        glEnable(GL_DEPTH_TEST);

        // 当把纹理添加到帧缓冲的时候, 所有的渲染操作会直接写到纹理colorTexture里面
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        if (m_texture_id == 0) {
            // 准备一个接收播放器Image Stream的纹理
            glGenTextures(1, &m_texture_id);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture_id);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          
            InitMediaTexture(m_texture_id);
        }
    
        // 更新纹理
        m_jni->CallVoidMethod(m_mediaTexture, m_media_texture_update_method, m_texture_matrix);

        // Clear swapchain and depth buffer.
        glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // Set shaders and uniform variables.
        glUseProgram(m_program);

        // Set cube primitive data.
        glBindVertexArray(m_vao);

        glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture_id);
        glUniform1i(m_texture_sampler, 0);
    
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ArraySize(Geometry::c_2dPlayerIndices)), GL_UNSIGNED_SHORT, nullptr);

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

    GLint m_vertexPosition{0};
    GLint m_texturePosition{0};
    GLint m_texture_sampler{0};
    GLuint m_rectVertexBuffer{0};
    GLuint m_rectIndecesBuffer{0};
    GLuint m_texture_id{0};
};
}  // namespace

std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin_OpenGLES(const std::shared_ptr<Options>& options,
                                                               std::shared_ptr<IPlatformPlugin> platformPlugin) {
    return std::make_shared<OpenGLESGraphicsPlugin>(options, platformPlugin);
}

#endif
