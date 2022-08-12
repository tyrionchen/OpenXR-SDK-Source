// Copyright (c) 2017-2022, The Khronos Group Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace Geometry {

struct Vertex {
    XrVector3f Position;
    XrVector3f Color;
};

constexpr XrVector3f Red{1, 0, 0};
constexpr XrVector3f DarkRed{0.25f, 0, 0};
constexpr XrVector3f Green{0, 1, 0};
constexpr XrVector3f DarkGreen{0, 0.25f, 0};
constexpr XrVector3f Blue{0, 0, 1};
constexpr XrVector3f DarkBlue{0, 0, 0.25f};

// Vertices for a 1x1x1 meter cube. (Left/Right, Top/Bottom, Front/Back)
// 1x1x1米方块的顶点, 字母分别表示: 左/右, 上/下, 前/后
constexpr XrVector3f LBB{-0.5f, -0.5f, -0.5f};
constexpr XrVector3f LBF{-0.5f, -0.5f, 0.5f};
constexpr XrVector3f LTB{-0.5f, 0.5f, -0.5f};
constexpr XrVector3f LTF{-0.5f, 0.5f, 0.5f};
constexpr XrVector3f RBB{0.5f, -0.5f, -0.5f};
constexpr XrVector3f RBF{0.5f, -0.5f, 0.5f};
constexpr XrVector3f RTB{0.5f, 0.5f, -0.5f};
constexpr XrVector3f RTF{0.5f, 0.5f, 0.5f};

#define CUBE_SIDE(V1, V2, V3, V4, V5, V6, COLOR) {V1, COLOR}, {V2, COLOR}, {V3, COLOR}, {V4, COLOR}, {V5, COLOR}, {V6, COLOR},

// 用Vertex的一维数组描述一个每面颜色不同的一个立方体:
// 这里如何描述一个面呢? 这里用了6个顶点
// 为什么用六个顶点呢? 因为想用两个三角形描述这个矩形的面
// 为什么用三角形描述呢? 这和图形API绘制方式有关
constexpr Vertex c_cubeVertices[] = {
    CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, DarkRed)    // -X, 立方体左面深红色
    CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Red)        // +X, 立方体右面红色
    CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, DarkGreen)  // -Y, 立方体底面深绿色
    CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Green)      // +Y, 立方体顶部绿色
    CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, DarkBlue)   // -Z, 立方体后面深蓝色
    CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Blue)       // +Z, 立方体前面蓝色
};

// Winding order is clockwise. Each side uses a different color.
// 前面这个c_cubeVertices立方体的绘制顺序，由于顶点Vertex数组已经考虑了绘制顺序了，所以这个地方直接就从0写到了35
constexpr unsigned short c_cubeIndices[] = {
    0,  1,  2,  3,  4,  5,   // -X
    6,  7,  8,  9,  10, 11,  // +X
    12, 13, 14, 15, 16, 17,  // -Y
    18, 19, 20, 21, 22, 23,  // +Y
    24, 25, 26, 27, 28, 29,  // -Z
    30, 31, 32, 33, 34, 35,  // +Z
};

// 图形顶点 
constexpr XrVector3f c_2dPlayer_vertices[] = {
    {1, 1, 0,},
    {-1, 1, 0},
    {-1, -1, 0},
    {1, -1, 0}
};

// 纹理顶点
constexpr XrVector3f c_2dPlayer_texture_vertices[] = {
    {1, 0, 0},
    {0, 0, 0},
    {0, 1, 0},
    {1, 1, 0}
};


constexpr unsigned short c_2dPlayerIndices[] = {
    3,0,1,3,2,1,3
};

}  // namespace Geometry
