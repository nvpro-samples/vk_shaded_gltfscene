#ifndef PAYLOAD_H
#define PAYLOAD_H

precision highp float;

#include "hit_state.h"

struct HitPayload
{
  uint     seed;
  float    hitT;
  int      rnodeID;
  int      rprimID;
  HitState hit;
};

#endif  // PAYLOAD_H