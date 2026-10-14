# 아키텍처

이 문서는 현재 코드의 모듈 경계, 저장 방식과 실행 흐름을 설명한다.
연구에서 요구하는 비교 구조와 실험 기준은 [연구·실험·논문 기준](research-goals.md)을 따른다.

## 모듈 책임과 의존 관계

| 모듈 | 책임 |
| --- | --- |
| `Engine/` | ECS 저장·조회, CPU Transform 계산, 공통 벤치마크와 기존 그래픽 런타임 |
| `Boids/Common/` | 공통 초기 설정과 `BoidParameters` |
| `Boids/ECS/` | Registry와 System으로 Boids 시뮬레이션 구현 |
| `Boids/OOP/` | BoidObject의 데이터와 메서드로 시뮬레이션 구현 |
| `Benchmarker/` | 초기화·warmup·고정 dt 측정 반복, 환경 수집과 결과 직렬화 |
| `App/` | 실행 옵션, 구현 선택, 콜백 연결과 결과 출력 |

`App`은 Engine과 Boids를 조립한다. Boids 행동은 `Boids`에 두고,
Engine에 도메인 로직을 넣거나 App에서 ECS 조회·Boids 계산을 수행하지 않는다.
`Boids::Common`은 Engine에 의존하지 않으며, OOP는 `Engine::ECS`에 의존하지 않는다.
두 시뮬레이션은 GLM만 사용하는 `Engine::RenderData`로 CPU Transform을 계산한다.

`Benchmarker::Experiment`는 실험 실행 함수를 제공한다. App은 구현을 선택하고 이
함수를 호출한다. 이 함수가 Boids를 직접 실행하며, Engine에는 콜백이나 시뮬레이션
타입을 전달하지 않는다. `Engine::Measurement`는 시간·메모리 계측 API만 제공한다.
Boids와 ECS 저장소는 이 API로 구간을 기록하고 소유한 저장량을 보고한다.

## 데이터와 저장 방식

- Sparse Set ECS: Registry가 개체 수명과 타입별 ComponentStorage를 소유한다.
  컴포넌트는 dense 배열에 저장하고, sparse index로 개체와 배열 위치를 연결한다.
  삭제 시 마지막 원소를 옮기고 index를 갱신한다.
- OOP: BoidSimulation이 `std::vector<BoidObject>`에 객체를 값으로 소유한다.
  각 객체가 Transform, 속도, 가속도와 BoidParameters를 보관한다.

두 구현은 기존 시각화용 MeshRenderer 데이터도 보관한다.
ECS의 View는 필요한 컴포넌트 조합을 조회하며, BoidSystem은 이웃 반복 조회에
CachedView의 개체·dense index를 재사용한다. 값 변경은 그대로 반영되고,
관련 저장 구조가 바뀌면 사용 전에 캐시를 갱신한다. 순회 중에는 구조를 변경하지 않는다.

ECS의 네 System은 모두 SystemManager가 `unique_ptr<System>`로 소유한다.
System의 공통 Update는 Registry, dt와 같은 TimingRecorder 참조를 받는다.
SystemManager는 등록 순서대로 실행하며, 두 인자 Update는 비활성 recorder를
만들어 같은 실행 경로를 사용한다. 개별 System의 두 인자 호출도 공통 System
오버로드를 사용한다.

## 실행 흐름

Headless 벤치마크는 다음 순서로 실행한다.

1. App이 옵션을 읽고 `Benchmarker::Experiment`의 실행 함수를 선택한다.
2. 실행 함수가 환경 정보를 수집하고, 시뮬레이션을 생성한 뒤 `Initialize()`를 계측한다.
   생성자와 계측 도구 준비는 초기화 시간에서 제외된다.
3. 계측을 비활성화한 채 고정 dt로 warmup을 실행한다.
4. step별 기록 공간을 미리 할당하고 `begin_step_measurement/end_step_measurement` 사이에서 Update를 호출한다.
   BoidSystem은 `start/stop`으로 조회 준비·이웃 계산을 계측한다.
   SystemManager는 상태 갱신 System들의 연속된 구간을 감싸서 계측한다.
5. 마지막 step 이후 프로세스 최대 메모리를 먼저 조회하고, 시뮬레이션 종료 전
   컨테이너 저장량과 시간 결과를 수집한다.
6. App이 CSV 원자료를 저장하고 설정·환경·시간·메모리 요약을 출력한다.

두 구현은 같은 seed와 설정으로 초기화하고 naive all-pairs 이웃 탐색을 사용한다.
한 step은 모든 Boid의 가속도 계산 → 속도·위치 갱신 → 경계 처리 → 방향 갱신 순서다.
ECS는 SystemManager에 등록된 Boid·Movement·Boundary·Orientation 순서로,
OOP는 객체 메서드의 반복 호출로 수행한다. BoidSystem은 조회 준비·이웃 계산을
내부에서 계측하며, 세 상태 갱신 System은 `AddTimed("state_update", ...)`로
등록한다. 매니저는 같은 이름으로 연속 등록된 System들을 하나의 start/stop 구간으로
감싸므로, 상태 갱신에는 세 System 실행과 그 사이의 디스패치가 포함된다.
계측 구간 이름은 Boids 등록 코드에서 제공하며 Engine에 하드코딩하지 않는다.
이웃 계산에는 모든 Boid의 갱신 전 위치·속도를 사용한다.

전체 step 계측은 Update 호출을 감싸며 초기화·warmup·출력은 제외한다.
구간 시간과 별도로 측정하고 구간 계측 비용을 포함한다. 원자료 복사·집계는 step
경계 밖에서 수행한다. 현재 Dummy 변경은 구현되지 않아 개체 변경 구간을 명시적
0으로 기록한다. OOP는 별도 조회 준비가 없어 해당 구간도 명시적 0으로 기록한다.

저장량은 시뮬레이션 객체 자체와 소유한 배열·개별 할당 객체의 size/capacity 기반
추정치다. ECS의 alive/generation/free ID 배열, component sparse/dense 배열과
CachedView를 포함한다. OOP의 현재 값 배열은 객체 데이터를 한 번만 계산한다.
SystemManager는 등록 항목 배열의 size/capacity와 등록 시 저장한 `sizeof(SystemType)`을
사용하여 실제 파생 타입의 개별 할당 객체를 보고한다. 각 System의 가상
AppendStorageReport는 별도 소유한 동적 저장소만 추가한다. 따라서 BoidSystem의
CachedView 배열도 매니저를 통해 보고되고, Simulation은 System 타입별 항목을 나열하지
않는다. 계측 구간 이름은 소유한 `vector<char>` 버퍼로 저장하며 이 버퍼의 size/capacity도
보고한다. 등록 항목의 제어 필드와 각 System의 객체 내부 필드는 별도로 중복 계산하지 않는다.
unordered_map의 항목 payload는 계산하지만 구현별 node 링크·bucket 저장량은
미지원으로 표시하며 ECS 합계는 알려진 항목의 부분합이다. allocator overhead와
fragmentation은 추정에 포함하지 않는다. 프로세스 최대값에는 계측 버퍼와 warmup도
포함되며, 조회 시점까지의 프로세스 전체 수명에 대한 값이다.

현재 비교 실행은 Boid 전용 ECS/OOP이며 연구 요구사항의 Dummy, 상속 기반 OOP,
Archetype은 아직 구현되지 않았다. 계측 API 계약과 연결 예시는
[벤치마크 API](benchmarker-api.md)를 참고한다.

Headless 경로는 그래픽 초기화를 수행하지 않는다. 기존 시각화는 별도로
Engine::Core의 Application 프레임 루프에서 프레임별 dt를 사용한다.
