
# libuv Sample

[libuv](https://github.com/libuv/libuv)는 크로스 플랫폼 비동기 I/O 라이브러리로, Windows와 UNIX 계열 시스템 모두에서 동작합니다.
이 샘플 코드는 libuv를 사용하여 비동기 I/O 작업을 처리합니다. 

* `uv_udp_t`를 사용하여 UDP 소켓을 처리합니다.
* `uv_timer_t`를 사용하여 주기적인 업데이트를 처리합니다.
* 클라이언트에서는 `uv_tty_t`를 사용하여 사용자 입력을 비동기적으로 처리합니다.
* 모든 I/O 작업은 비동기적으로 처리되며, 콜백 함수를 통해 결과를 처리합니다.

## Server and Client

이 예제에서는 다음과 같은 과정을 거칩니다:

1. 서버와 클라이언트 모두 reliable_endpoint를 생성합니다.
2. 각각 transmit_packet 함수와 process_packet 함수를 정의하여 패킷 송수신을 처리합니다.
3. 클라이언트는 사용자의 입력에 따라 해당 메시지를 서버로 전송합니다. 예) 1: move, 2: attack, 3: use, q: 실행 중지.
4. 서버는 클라이언트로부터 받은 메시지에 대한 응답을 화면에 출력합니다.
5. 양쪽 모두 receive_packet 함수를 통해 받은 패킷을 reliable 라이브러리로 전달합니다.
주기적으로 reliable_endpoint_update를 호출하여 라이브러리 내부 상태를 업데이트합니다.

코드 

1. ServerContext와 ClientContext에 running 플래그를 추가하여 메인 루프의 실행을 제어합니다.
2. 메인 함수에서 while 루프를 사용하여 이벤트 루프를 실행합니다. 각 반복에서:
    * `uv_run(ctx.loop, UV_RUN_NOWAIT)`를 호출하여 대기 중인 이벤트를 처리합니다.
    * `reliable_endpoint_update`를 호출하여 reliable 라이브러리의 상태를 업데이트합니다.
    * ACK를 처리합니다.
    * `uv_sleep(16)`을 호출하여 약 60 FPS로 실행되도록 합니다.
3. 클라이언트에서 'q' 입력 시 running 플래그를 0으로 설정하여 루프를 종료합니다.
4. 루프 종료 후 정리 작업을 수행합니다:   
    * reliable 엔드포인트를 파괴합니다.
    * 모든 핸들을 닫습니다.
    * 남은 이벤트를 처리합니다.
    * 이벤트 루프를 정리합니다.


이 방식은 이벤트 루프를 더 세밀하게 제어할 수 있게 해주며, 필요한 경우 루프 사이에 추가 작업을 수행할 수 있습니다. 또한 프로그램의 종료를 더 쉽게 제어할 수 있습니다.

## 패킷 손실 처리 

이 예제에서 reliable 라이브러리가 패킷 손실을 처리하는 방법은 다음과 같습니다:

1. **시퀀스 번호**: 각 패킷에는 시퀀스 번호가 할당됩니다. 이를 통해 패킷의 순서와 누락된 패킷을 추적할 수 있습니다.
2. **ACK 메커니즘**: 수신자는 성공적으로 받은 패킷에 대해 ACK(승인)을 보냅니다. 이는 reliable_endpoint_get_acks 함수를 통해 확인할 수 있습니다.
3. **재전송**: ACK를 받지 못한 패킷은 자동으로 재전송됩니다. 이는 라이브러리 내부에서 처리됩니다.
4. **타임아웃**: 라이브러리는 내부적으로 타임아웃을 사용하여 일정 시간 동안 ACK가 오지 않으면 패킷을 재전송합니다.
5. **중복 패킷 처리**: 수신자는 중복 패킷을 자동으로 감지하고 무시합니다.

이 메커니즘들은 `reliable_endpoint_update` 함수 호출을 통해 주기적으로 처리됩니다. 이 함수는 내부적으로 ACK 확인, 재전송 필요 여부 확인 등을 수행합니다.
코드에서 볼 수 있듯이, 우리는 직접적으로 패킷 손실을 처리하지 않습니다. 대신 reliable 라이브러리가 제공하는 함수들을 사용하여 패킷을 송수신하고, 주기적으로 `reliable_endpoint_update`를 호출하여 라이브러리가 패킷 손실을 처리하도록 합니다.

또한, `reliable_endpoint_get_acks`를 통해 어떤 패킷이 성공적으로 전달되었는지 확인할 수 있습니다. 이는 디버깅이나 네트워크 상태 모니터링에 유용할 수 있습니다.
이 예제를 실행하면, 클라이언트에서 사용자 입력에 따라 메시지를 보내고, 서버는 받은 메시지를 출력합니다. 패킷 손실이 발생하더라도 reliable 라이브러리가 자동으로 재전송을 처리하므로, 최종적으로는 모든 메시지가 전달됩니다.

## ACK 처리

`reliable_endpoint_get_acks` 함수는 성공적으로 전달된 패킷의 시퀀스 번호 목록을 제공합니다. 이 정보는 여러 가지 용도로 활용될 수 있습니다. 

```c
uint16_t * reliable_endpoint_get_acks( reliable_endpoint_t * endpoint, int * num_acks );
```

사용방법

```c
int num_acks;
uint16_t* acks = reliable_endpoint_get_acks(endpoint, &num_acks);
for (int i = 0; i < num_acks; i++) {
    printf("Acked packet %d\n", acks[i]);
}
```

사용사례

a. 디버깅:
어떤 패킷이 성공적으로 전달되었는지 확인할 수 있습니다.
패킷 손실이 발생했을 때 어떤 패킷이 누락되었는지 파악할 수 있습니다.

아래 코드는 패킷 전송 성공률을 계산하는 코드입니다. 
```c
int total_sent = reliable_endpoint_get_sent_packets(endpoint);
int num_acks;
reliable_endpoint_get_acks(endpoint, &num_acks);
float success_rate = (float)num_acks / total_sent;
printf("Packet transmission success rate: %.2f%%\n", success_rate * 100);
```

b. 네트워크 상태 모니터링:
ACK된 패킷의 수와 비율을 추적하여 네트워크의 안정성을 평가할 수 있습니다.
시간에 따른 ACK 패턴을 분석하여 네트워크 성능의 변화를 감지할 수 있습니다.

아래 코드는 특정 중요 패킷의 전달을 확인하는 코드입니다. 
```c
int num_acks;
uint16_t* acks = reliable_endpoint_get_acks(endpoint, &num_acks);
uint16_t important_packet_seq = 1234;
bool important_packet_acked = false;
for (int i = 0; i < num_acks; i++) {
    if (acks[i] == important_packet_seq) {
        important_packet_acked = true;
        break;
    }
}
if (important_packet_acked) {
    printf("Important packet %d was successfully delivered\n", important_packet_seq);
}
```

c. 애플리케이션 로직:
중요한 데이터가 성공적으로 전달되었는지 확인할 수 있습니다.
특정 패킷의 전달 확인 후 다음 단계의 로직을 진행할 수 있습니다.

아래 코드는 네트워크 지연 분석을 하는 코드입니다. 
```c
int num_acks;
uint16_t* acks = reliable_endpoint_get_acks(endpoint, &num_acks);
double current_time = get_current_time();
for (int i = 0; i < num_acks; i++) {
    double send_time = get_packet_send_time(acks[i]);
    double delay = current_time - send_time;
    printf("Packet %d ACKed after %.3f seconds\n", acks[i], delay);
}
```

주의: 
* `reliable_endpoint_get_acks`를 호출한 후에는 반드시 `reliable_endpoint_clear_acks`를 호출하여 ACK 목록을 초기화해야 합니다.
* ACK 정보는 매 프레임마다 갱신되므로, 필요한 시점에 적절히 호출해야 합니다.

