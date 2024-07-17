#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "reliable.h"
#include "uvcommon/serialize.h"
#include <iostream>

#define MAX_PACKET_SIZE 1024
#define SERVER_PORT 12345

typedef struct
{
  uv_udp_t udp_handle;
  struct sockaddr_in server_addr;
  reliable_endpoint_t* endpoint;
  uv_loop_t* loop;
  int running;
} ClientContext;

static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

static void client_transmit_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes)
{
  ClientContext* ctx = (ClientContext*)context;
  uv_buf_t buf = uv_buf_init((char*)packet_data, packet_bytes);
  uv_udp_try_send(&ctx->udp_handle, &buf, 1, (const struct sockaddr*)&ctx->server_addr);
  printf("Client sent packet: seq=%d, size=%d\n", sequence, packet_bytes);
}

static int client_process_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes)
{
  printf("Client received: seq=%d, size=%d\n", sequence, packet_bytes);
  return 1;
}

static void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
  if (nread > 0) {
    ClientContext* ctx = (ClientContext*)handle->data;
    reliable_endpoint_receive_packet(ctx->endpoint, (uint8_t*)buf->base, nread);
  }
  if (buf->base)
    free(buf->base);
}

int main()
{
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) 
  {
    std::cerr << "Failed to initialize Winsock" << std::endl;
    return 1;
  }
#endif

  ClientContext ctx;
  ctx.loop = uv_default_loop();
  ctx.running = 1;

  struct reliable_config_t config;
  reliable_default_config(&config);
  config.transmit_packet_function = client_transmit_packet;
  config.process_packet_function = client_process_packet;
  config.context = &ctx;
  ctx.endpoint = reliable_endpoint_create(&config, uv_now(ctx.loop) / 1000.0);

  uv_udp_init(ctx.loop, &ctx.udp_handle);
  ctx.udp_handle.data = &ctx;
  uv_ip4_addr("127.0.0.1", SERVER_PORT, &ctx.server_addr);
  uv_udp_recv_start(&ctx.udp_handle, alloc_buffer, on_read);

  struct packet::message msg;
  memset(&msg, 0, sizeof(msg));
  strncpy_s(msg.name, sizeof(msg.name), "weapon-type", sizeof(msg.name) - 1);
  msg.id = 1;

  while (ctx.running)
  {
    printf("Enter command (1: safety, 2: fire, 3: reload, q: quit): ");
    fflush(stdout);
    char input[2];
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
      break;
    }

    switch (input[0])
    {
    case '1':
      msg.state_01 = 0;     // safety
      msg.thumb_x = 1.0f;
      msg.thumb_y = 0.0f;
      break;
    case '2':
      msg.state_01 = 1;     // fire
      msg.buttonState = 1;  // Assuming button 1 is for attack
      break;
    case '3':
      msg.state_01 = 2;     // reload
      msg.buttonState = 2;  // Assuming button 2 is for use
      break;
    case 'q':
      ctx.running = 0;
      continue;
    default:
      continue;
    }

    uint8_t buffer[sizeof(struct packet::message)];
    packet::serialize(&msg, buffer);
    reliable_endpoint_send_packet(ctx.endpoint, buffer, sizeof(buffer));
    //reliable_endpoint_send_packet(ctx.endpoint, (uint8_t*)&msg, sizeof(msg));

    uv_run(ctx.loop, UV_RUN_NOWAIT);
    reliable_endpoint_update(ctx.endpoint, uv_now(ctx.loop) / 1000.0);

    int num_acks;
    uint16_t* acks = reliable_endpoint_get_acks(ctx.endpoint, &num_acks);
    for (int i = 0; i < num_acks; i++)
    {
      printf("Client: Acked packet %d\n", acks[i]);
    }
    reliable_endpoint_clear_acks(ctx.endpoint);

    // Reset packet state after sending
    msg.state_01 = 0;
    msg.buttonState = 0;
    msg.thumb_x = 0.0f;
    msg.thumb_y = 0.0f;

    uv_sleep(16);
  }

  reliable_endpoint_destroy(ctx.endpoint);
  uv_close((uv_handle_t*)&ctx.udp_handle, NULL);
  while (uv_loop_alive(ctx.loop))
  {
    uv_run(ctx.loop, UV_RUN_NOWAIT);
  }
  uv_loop_close(ctx.loop);

#ifdef _WIN32
  WSACleanup();
#endif

  return 0;
}