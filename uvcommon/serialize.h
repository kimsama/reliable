//
// serialize.h
//

#pragma once
#include <cstdint>

namespace packet
{
  #pragma pack(push, 1)
  struct message
  {
    char name[16];
    unsigned short id;
    char state_01;              
    char state_02;              
    unsigned short buttonState; 
    float thumb_x;              
    float thumb_y;              
  };
  #pragma pack(pop)

  void serialize(const struct message* msg, uint8_t* buffer);

  void deserialize(const uint8_t* buffer, struct message* msg);
}
