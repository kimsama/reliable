//
// serialize.cc
//

#include <string.h>

// htons, ntohs, htonl, ntohl
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#elif
#include <arpa/inet.h>  
#endif

#include "serialize.h"

namespace packet
{
  void serialize(const struct message* msg, uint8_t* buffer)
  {
      int offset = 0;
      memcpy(buffer + offset, msg->name, 16);
      offset += 16;
      *(uint16_t*)(buffer + offset) = htons(msg->id);
      offset += 2;
      *(buffer + offset) = msg->state_01;
      offset += 1;
      *(buffer + offset) = msg->state_02;
      offset += 1;
      *(uint16_t*)(buffer + offset) = htons(msg->buttonState);
      offset += 2;
      *(uint32_t*)(buffer + offset) = htonl(*(uint32_t*)&msg->thumb_x);
      offset += 4;
      *(uint32_t*)(buffer + offset) = htonl(*(uint32_t*)&msg->thumb_y);
  }

  void deserialize(const uint8_t* buffer, struct message* msg) 
  {
    int offset = 0;
    memcpy(msg->name, buffer + offset, 16);
    offset += 16;
    msg->id = ntohs(*(uint16_t*)(buffer + offset));
    offset += 2;
    msg->state_01 = *(buffer + offset);
    offset += 1;
    msg->state_02 = *(buffer + offset);
    offset += 1;
    msg->buttonState = ntohs(*(uint16_t*)(buffer + offset));
    offset += 2;
    uint32_t temp;
    temp = ntohl(*(uint32_t*)(buffer + offset));
    msg->thumb_x = *(float*)&temp;
    offset += 4;
    temp = ntohl(*(uint32_t*)(buffer + offset));
    msg->thumb_y = *(float*)&temp;
  }
}
