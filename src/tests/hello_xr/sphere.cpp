//
// Created by cyy on 2022/8/11.
//

#include "sphere.h"
#include "pch.h"
#include "common.h"

//将球横纵划分成50*50的网格
const int Y_SEGMENTS = 50;
const int X_SEGMENTS = 50;

Sphere::Sphere() {
  float PI = MATH_PI;
  vertexs_size = Y_SEGMENTS * X_SEGMENTS * 3;
  vertexs = new float[vertexs_size];
  texcoords_size = Y_SEGMENTS * X_SEGMENTS * 2;
  texcoords = new float[texcoords_size];
  indices_size = Y_SEGMENTS * X_SEGMENTS * 6;
  indices = new unsigned short[indices_size];
  memset(vertexs, 0, sizeof (vertexs));
  memset(texcoords, 0, sizeof (texcoords));
  memset(indices, 0, sizeof (indices));
  
  /*2-计算球体顶点*/
  //生成球的顶点
  int t = 0, v = 0;
  for (auto y = 0; y <= Y_SEGMENTS; y++)
    for (auto x = 0; x <= X_SEGMENTS; x++) {
      float xSegment = (float) x / (float) X_SEGMENTS;
      float ySegment = (float) y / (float) Y_SEGMENTS;
      float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
      float yPos = std::cos(ySegment * PI);
      float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

      texcoords[t++] = xSegment;
      texcoords[t++] = ySegment;

      vertexs[v++] = xPos;
      vertexs[v++] = yPos;
      vertexs[v++] = zPos;
    }

  //生成球的Indices
  int counter = 0;
  for (int i=0;i<Y_SEGMENTS;i++)
  {
    for (int j=0;j<X_SEGMENTS;j++)
    {
      indices[counter++] = i * (X_SEGMENTS + 1) + j;
      indices[counter++] = (i + 1) * (X_SEGMENTS + 1) + j;
      indices[counter++] = (i + 1) * (X_SEGMENTS + 1) + j+1;
      indices[counter++] = i * (X_SEGMENTS + 1) + j;
      indices[counter++] = (i + 1) * (X_SEGMENTS + 1) + j + 1;
      indices[counter++] = i * (X_SEGMENTS + 1) + j + 1;
    }
  }
}
