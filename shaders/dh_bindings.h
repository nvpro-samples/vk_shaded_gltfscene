#ifndef HD_BINDINGS_H
#define HD_BINDINGS_H

// clang-format off
#ifdef __cplusplus // Descriptor binding helper for C++ and GLSL
#define START_BINDING(a) enum a {
#define END_BINDING() }
#define INLINE inline
#else
#define START_BINDING(a)  const uint
#define END_BINDING()
#define INLINE
#endif


START_BINDING(SceneBindings)
eFrameInfo = 0,
eSceneDesc = 1,
eTextures = 2
END_BINDING();

START_BINDING(RtxBindings)
eTlas = 0,
eOutImage = 1,
eNormalDepth = 2,
eSelect = 3
END_BINDING();

START_BINDING(DeferredBindings)
eDeferredOutImage = 0,
eDeferredData = 1
END_BINDING();

START_BINDING(PostBindings)
ePostImage = 0
END_BINDING();




#endif  // HD_BINDINGS_H
