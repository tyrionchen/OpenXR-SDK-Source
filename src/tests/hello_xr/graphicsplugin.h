// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

struct Cube {
    XrPosef Pose;
    XrVector3f Scale;
};

#include "common.h"

// Wraps a graphics API so the main openxr program can be graphics API-independent.
// 图形API接口封装，让openxr程序能拿个实现跨图形API的绘制逻辑
struct IGraphicsPlugin {
    virtual ~IGraphicsPlugin() = default;

    // OpenXR extensions required by this graphics API.
    // 获取OpenXR扩展需要的图形API
    virtual std::vector<std::string> GetInstanceExtensions() const = 0;

    // Create an instance of this graphics api for the provided instance and systemId.
    // 图形接口初始化
    virtual void InitializeDevice(XrInstance instance, XrSystemId systemId) = 0;

    // Select the preferred swapchain format from the list of available formats.
    // 从拿到的所有swap chain格式里面选择要使用的某一个
    virtual int64_t SelectColorSwapchainFormat(const std::vector<int64_t>& runtimeFormats) const = 0;

    // Get the graphics binding header for session creation.
    // 返回XrSessionCreateInfo需要使用的next字段指，这个指是一个graphics API binding structure指针
    virtual const XrBaseInStructure* GetGraphicsBinding() const = 0;

    // Allocate space for the swapchain image structures. These are different for each graphics API. The returned
    // pointers are valid for the lifetime of the graphics plugin.
    virtual std::vector<XrSwapchainImageBaseHeader*> AllocateSwapchainImageStructs(
        uint32_t capacity, const XrSwapchainCreateInfo& swapchainCreateInfo) = 0;

    // Render to a swapchain image for a projection view.
    virtual void RenderView(const XrCompositionLayerProjectionView& layerView, const XrSwapchainImageBaseHeader* swapchainImage,
                            int64_t swapchainFormat, const std::vector<Cube>& cubes) = 0;

    // Get recommended number of sub-data element samples in view (recommendedSwapchainSampleCount)
    // if supported by the graphics plugin. A supported value otherwise.
    virtual uint32_t GetSupportedSwapchainSampleCount(const XrViewConfigurationView& view) {
        return view.recommendedSwapchainSampleCount;
    }

    virtual void SetXrBaseInStructure(XrBaseInStructure* baseInStructure) {
        Log::Write(Log::Level::Info, Fmt("SetXrBaseInStructure:%p", baseInStructure));
    }
    virtual void SetPos(XrPosef* baseInStructure) {
      Log::Write(Log::Level::Info, Fmt("SetPosSetPos:%p", baseInStructure));
    }
};

// Create a graphics plugin for the graphics API specified in the options.
std::shared_ptr<IGraphicsPlugin> CreateGraphicsPlugin(const std::shared_ptr<struct Options>& options,
                                                      std::shared_ptr<struct IPlatformPlugin> platformPlugin);
