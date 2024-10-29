# Device Driver GPIO 제어 실습
디바이스 드라이버와 커널 타이머를 활용하여 LED 제어와 타이머 관리 기능을 구현하는 시스템

# Purpose
- LED, 버튼의 GPIO 제어를 부트로더, 시스템 콜, 디바이스 드라이버로 각각 구현
- 디바이스 드라이버에서 각 기능을 독립적으로 실습
- 실습한 디바이스 드라이버 기능을 결합하여 결과를 도출하는 평가 진행

# System Architecture Diagram
![스크린샷 2024-10-29 222128](https://github.com/user-attachments/assets/76d85b0f-9bb1-49d7-8a6a-ef092597f261)
### 1)　User Application
- Application 실행 및 종료: Application을 실행하면 Timer Interrupt가 시작되며 인자에 따라 LED가
주기적으로 점멸. Application이 종료될 때 Timer Interrupt도 함께 종료.
- 입출력 다중화: 버튼 입력과 키보드 입력을 하나의 프로세스에서 처리할 수 있도록 입출력 다중화
구현.
- 명령어 제어: ioctl()을 통해 버튼의 각 키에 맞춰 다양한 기능을 수행
  - Key 1: 커널 타이머 정지
  - Key 2: 타이머 값 설정
  - Key 3: LED 값 설정
  - Key 4: 커널 타이머 시작
  - Key 8: Application 종료
### 2) Device Driver
- 모듈 로드: insmod로 모듈이 로드되면 kerneltimer_init() 함수가 호출되어 드라이버 초기화 작업을
수행. 디바이스를 등록하고, LED와 버튼에 필요한 GPIO 핀을 설정하여 준비 상태로 만듦.
- GPIO 및 인터럽트 초기화: 드라이버가 열리면 LED와 버튼에 할당된 GPIO 핀을 초기화하고, 버튼
입력을 감지할 수 있도록 인터럽트를 설정.
- 타이머 기반 LED 제어: timerVal에 따라 타이머를 설정하여 주기적으로 LED가 점멸되도록 제어.
타이머 만료 시 LED on/off 상태를 반전하며, 새로운 주기로 타이머를 등록해 연속적인 동작을 유지.
- 명령어 처리: ioctl()을 통해 Application으로부터 전달된 명령어를 처리 (명령어는 헤더파일에 정의)
  - TIMER_START: 커널 타이머 시작
  - TIMER_STOP: 커널 타이머 정지
  - TIMER_VALUE: 타이머 주기 설정
- 자원 관리: 드라이버가 닫힐 때 할당된 동적 메모리를 해제하고, 타이머와 인터럽트를 정리하여
시스템 자원을 안전하게 관리.

# Result
![image](https://github.com/user-attachments/assets/4c6bdbcc-65cb-48c8-8a1d-4efb6cc1907d)
