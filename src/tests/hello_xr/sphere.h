//
// Created by cyy on 2022/8/11.
//

#ifndef HELLO_XR_SPHERE_H
#define HELLO_XR_SPHERE_H

#include "common/gfxwrapper_opengl.h"

class Sphere {
 public: 
  Sphere();
  virtual ~Sphere() = default;

public:
  float *vertexs;
  float *texcoords;
  unsigned short *indices;
  
  size_t vertexs_size;
  size_t texcoords_size;
  int indices_size;
};


#endif //HELLO_XR_SPHERE_H
