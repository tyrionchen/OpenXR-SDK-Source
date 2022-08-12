//
// Created by cyy on 2022/8/11.
//

#include "player.h"
#include "common/gfxwrapper_opengl.h"
#include <common/xr_linear.h>
#include <jni.h>
#include "android_common.h"
#include "geometry.h"
#include "sphere.h"

// The version statement has come on first line.
static const char* VertexShaderGlsl = R"_(#version 320 es
    in vec3 aPosition;
    in vec3 aTexCoord;
    uniform mat4 uMatrix;
    out vec3 vTexCoord;

    void main() {
       vTexCoord = aTexCoord;
       gl_Position = uMatrix * vec4(aPosition, 1.0);
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

struct SpherePlayer: public IPlayer {

    SpherePlayer(const std::array<float, 4> color) :
        m_clearColor(color), s_sphere(std::make_shared<Sphere>()) {
    }
  
  void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage) override {
    
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
  
//          glCullFace(GL_BACK);
//          glEnable(GL_CULL_FACE);
          
//          glEnable(GL_DEPTH_TEST);
  
      // 当把纹理添加到帧缓冲的时候, 所有的渲染操作会直接写到纹理colorTexture里面
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
  
      if (m_texture_id == 0) {
        // 准备一个接收播放器Image Stream的纹理
        glGenTextures(1, &m_texture_id);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_texture_id);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  
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
  
      const auto& pose = layerView.pose;
      XrMatrix4x4f proj;
      XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_OPENGL_ES, layerView.fov, 0.0f, 500.0f);

      XrMatrix4x4f toView;
      XrVector3f scale{1.f, 1.f, 1.f};
      XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);
      XrMatrix4x4f view;
      XrMatrix4x4f_InvertRigidBody(&view, &toView);
      XrMatrix4x4f vp;
      XrMatrix4x4f_Multiply(&vp, &proj, &view);
      XrMatrix4x4f translation;
      XrMatrix4x4f_CreateTranslation(&translation, 0, 0, 0);
//      XrMatrix4x4f mvp;
//      XrMatrix4x4f_Multiply(&mvp, &vp, &translation);
    
      glUniformMatrix4fv(m_matrix_handle, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(&vp));
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(s_sphere->indices_size), GL_UNSIGNED_SHORT, nullptr);
  
      glBindVertexArray(0);
      glUseProgram(0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void SetPos(XrPosef* pose) {
    m_pose = pose;
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
  
  void InitializeResources() override {
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
    // 矩阵
    m_matrix_handle = glGetUniformLocation(m_program, "uMatrix");

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // 图形顶点
    glGenBuffers(1, &m_rectVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_rectVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, s_sphere->vertexs_size * sizeof(float), s_sphere->vertexs, GL_STATIC_DRAW);
    // 指定顶点属性
    glEnableVertexAttribArray(m_vertexPosition);
    glVertexAttribPointer(m_vertexPosition, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

    // 纹理顶点
    glGenBuffers(1, &m_textureVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_textureVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, s_sphere->texcoords_size * sizeof(float), s_sphere->texcoords, GL_STATIC_DRAW);
    // 指定顶点属性
    glEnableVertexAttribArray(m_texturePosition);
    glVertexAttribPointer(m_texturePosition, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);

    // 索引数据
    glGenBuffers(1, &m_rectIndecesBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_rectIndecesBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, s_sphere->indices_size * sizeof(unsigned short), s_sphere->indices, GL_STATIC_DRAW);
  }
  
  void InitMediaTexture(int textureId) {
    jclass media_texture_clz = loadClz("com/khronos/hello_xr/MediaTexture");
    Log::Write(Log::Level::Info, Fmt("InitMediaTexture m_context:%p media_texture_clz:%p", m_context, media_texture_clz));
    jmethodID media_texture_constructor = m_jni->GetMethodID(media_texture_clz, "<init>", "(ILandroid/content/Context;)V");
    m_mediaTexture = m_jni->NewObject(media_texture_clz, media_texture_constructor, textureId, (jobject)m_context);
    m_media_texture_update_method = m_jni->GetMethodID(media_texture_clz, "updateTexture", "([F)V");
    m_texture_matrix = m_jni->NewFloatArray(16);
  }

  ~SpherePlayer() override {
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

  const std::array<float, 4> m_clearColor;
  GLint m_vertexPosition{0};
  GLint m_texturePosition{0};
  GLint m_matrix_handle{0};
  GLint m_texture_sampler{0};
  GLuint m_rectVertexBuffer{0};
  GLuint m_textureVertexBuffer{0};
  GLuint m_rectIndecesBuffer{0};
  GLuint m_texture_id{0};
  GLuint m_swapchainFramebuffer{0};
  GLuint m_program{0};
  GLuint m_vao{0};
  GLuint m_cubeVertexBuffer{0};
  GLuint m_cubeIndexBuffer{0};
  std::shared_ptr<Sphere> s_sphere;
  XrPosef* m_pose;
};


std::shared_ptr<IPlayer> createPlayer(const std::array<float, 4> color) {
  return std::make_shared<SpherePlayer>(color);
}
