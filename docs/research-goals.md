# 졸업 논문 통합 계획

갱신일: 2026-10-14

이 문서는 연구·실험·논문 기준을 관리한다. 작업과 진행 상태는 GitHub 이슈에서 관리한다.

## 목차

1. [연구 주제와 목표](#research)
2. [연구 질문](#questions)
3. [범위와 아키텍처 원칙](#scope)
4. [각 구현의 개요 및 공통 시뮬레이션 규칙](#implementations)
5. [실험 설계와 조건 통제](#experiments)
6. [측정 지표와 계측 경계](#metrics)
7. [통계 분석과 결과 해석](#analysis)
8. [논문 구성과 필요한 표·그림](#paper)
9. [최종 검증·제출 기준](#completion)
10. [참고문헌과 근거 자료](#references)

<a id="research"></a>

## 1. 연구 주제와 목표

### 논문 제목

국문: **Boids 시뮬레이션에서 OOP, Sparse Set ECS 및 Archetype ECS의 성능 비교**

영문: **A Performance Comparison of OOP, Sparse Set ECS, and Archetype ECS
in a Boids Simulation**

### 배경과 연구 목적

ECS(Entity-Component-System)의 실행 비용은 컴포넌트 저장 방식과 반복 접근
경로에 따라 달라진다. Sparse Set은 타입별 저장소와 sparse/dense index를 사용하고,
Archetype은 동일한 컴포넌트 조합을 column으로 저장한다. OOP는 공통 부모에서
Boid와 Dummy를 상속하고, 부모 포인터를 통해 각 객체의 데이터와 메서드에 접근한다.

동일한 단일 스레드 3D Boids Simulation을 위 세 구조로 구현하고, 알고리즘과 초기
상태를 통제하여 성능을 정량적으로 분석한다. 특정 구조가 항상
빠르다는 결론을 전제하지 않는다. 관측된 차이를 데이터 배치, 쿼리·index 접근과
phase별 처리 비용 관점에서 설명한다.

연구 목표는 다음과 같다.

1. 경량 ECS 런타임과 headless(렌더링 없이)로 실행 가능한 3D Boids 시뮬레이션을 완성한다.
2. OOP, Sparse Set ECS, Archetype ECS로 동일한 시뮬레이션을 구현·검증한다.
3. Boid·Dummy 수와 Dummy 삭제·재생성 비율에 따른 전체 step 성능을 비교한다.
4. 개체 변경·조회 준비·이웃 계산·상태 갱신 시간을 통해 저장·접근 비용을 분석한다.
5. 초기화 시간, 프로세스 최대 메모리와 컨테이너 저장량을 비교한다.
6. 원자료로 재생성 가능한 표·그래프와 RQ(Research Question)별 결론을 졸업 논문으로 제시한다.

### 선행연구와 기여

[Reynolds(1987)](https://www.red3d.com/cwr/papers/1987/boids.html)는 개별 개체의
지역적 행동으로 군집 움직임을 만드는 Boids 모델의 출발점이다. 본 구현은
그 모델에서 착안한 단순화된 3D 실험 시뮬레이션이며 원전의 모든 기능을 재현하지 않는다.

[Cox 등(2025)](https://diglib.eg.org/items/6e291ae6-e32c-4c21-a89b-021fd9986ede)은
Sparse Set과 Archetype ECS의 순회 및 개체 구성 변경 비용을 비교한다.
본 연구는 3D Boids에서 상속 기반 OOP를 비교 기준으로 추가하고,
CachedView 적용 전후의 보조 비교, phase별 시간, 초기화와 메모리를 함께 분석한다.
기존 연구의 결론을 그대로 재현한다고 가정하지 않는다.

<a id="questions"></a>

## 2. 연구 질문

| 연구 질문 | 필요한 근거 | 논문에서 답할 내용 |
| --- | --- | --- |
| RQ1. 세 구조의 전체 step 성능은 개체 수와 Dummy 변경 비율에 따라 어떻게 달라지는가? | 동일 조건의 전체 `ms/step`, 산포, 구현 간 시간 비율과 신뢰구간 | 개체 수·변경 비율에 따른 성능 차이 |
| RQ2. 저장 구조 변경과 데이터 순회·조회 방식은 성능 차이와 어떤 관계가 있는가? | 네 측정 구간의 시간, 데이터 순회·조회 방식, View/CachedView 보조 실험과 가능한 캐시 측정 | 개체 변경·조회 준비 비용과 계산 비용의 차이 |
| RQ3. 세 구조의 초기화 시간과 메모리 비용은 어떻게 다른가? | 초기화 범위·reserve 정책, 프로세스 메모리, 보조 구조를 포함한 컨테이너 저장량 | 생성 비용, 용량 정책, bytes per entity |

<a id="scope"></a>

## 3. 범위와 아키텍처 원칙

### 경량 게임 엔진의 정의

게임 엔진은 실시간 실행과 시뮬레이션 검증에 필요한 최소 런타임을 의미한다.

- 시뮬레이션 초기화·종료 및 고정 시간 간격의 반복 실행
- World와 ECS 기반 객체 관리: Entity 생성·삭제, Component 추가·조회, System 실행
- Boids 행동 계산과 위치·속도·방향 갱신
- Headless benchmark와 시간·메모리 측정

Boids는 이 런타임 위에서 실행되는 연구용 시뮬레이션이다. 대량 NPC·군중·파티클성 객체를
처리하는 상황을 단순화하지만, 결과를 모든 게임 workload에 일반화하지 않는다.
주 실험은 창과 OpenGL 컨텍스트 없이 CPU simulation만 실행한다.

### 기술과 모듈 경계

| 항목 | 기준 |
| --- | --- |
| 언어·빌드 | C++20, CMake, Visual Studio 2022 기본 검증, Release 성능 측정 |
| 의존성 | vcpkg 사용, 연구에 필요한 의존성만 추가 |
| `Engine/` | ECS, CPU 상태 데이터와 Benchmark 등 재사용 런타임 |
| `Boids/Common/` | 공통 설정과 `BoidParameters`; Engine/ECS에 의존하지 않음 |
| `Boids/OOP/` | 상속 기반 객체 시뮬레이션; `Engine/ECS`에 의존하지 않음 |
| `Boids/ECS/` 및 Archetype 구현 | ECS 기반 저장·시스템과 연구 도메인 행동 |
| `App/` | 구현 선택, 콜백 연결, 옵션·출력을 담당하는 얇은 조립 계층 |

`Engine`에 Boids 행동을 넣지 않는다. `App`에 ECS 쿼리나 Boids 시스템을 구현하지
않는다. 위치·회전·크기 데이터와 관련 계산 코드는 OpenGL 렌더링 코드와 분리한다. 시뮬레이션에서는 GLM 수학 라이브러리만 사용하는 Engine::RenderData Cmake 빌드 타겟을 통해 이를 사용한다. 시뮬레이션에 OpenGL 실행 의존성을 추가하지 않는다.
헤더와 소스는 기능별 폴더에 함께 둔다.

### 제외 범위

렌더링·시각화의 추가 구현과 FPS·frame time·render GPU 관련 측정은 프로젝트
완료 범위와 논문에서 제외한다. 3D는 위치·속도·행동 계산의 공간 차원을 뜻한다.
(외부 대회 포스터 시각 자료 용으로만 제한적으로 사용할 예정)

<a id="implementations"></a>

## 4. 각 구현의 개요 및 공통 시뮬레이션 규칙

### 최종 비교 경로

| 구현 | 저장 방식 | 주요 접근 경로 |
| --- | --- | --- |
| OOP | `std::vector<std::unique_ptr<SimulationObject>>` | 공통 부모 포인터로 객체에 접근하고 메서드 호출 |
| Sparse Set ECS | 타입별 sparse index, dense entity·component 배열 | 이웃 `CachedView`의 Entity·저장소별 dense index 재사용 |
| Archetype ECS | Boid·Dummy의 component 조합별 column 배열 | Boid에 해당하는 묶음의 column과 행 범위 직접 순회 |

OOP의 공통 부모 `SimulationObject`는 Transform과 Velocity를 가진다.
`BoidObject`는 이를 상속하고 Acceleration과 BoidParameters를 추가하며,
`DummyObject`는 공통 데이터만 가진다. 두 종류를 같은 부모 포인터 배열에 저장하고
각 객체의 수명은 `unique_ptr`로 관리한다. 연속된 것은 포인터 배열이며 객체 데이터가
연속 저장된다고 가정하지 않는다.

### Dummy와 개체 변경

- ECS의 Dummy는 `Transform + Velocity`를 가지며 `BoidParameters`는 가지지 않는다.
  OOP의 DummyObject도 공통 부모의 Transform과 Velocity만 가진다.
- Dummy는 이웃 탐색을 포함한 모든 Boids 계산·상태 갱신에서 제외한다.
- 초기 생성은 Boid와 Dummy를 번갈아 배치하고, 남는 개체는 이어서 생성한다.
- 매 step Dummy 중 `n%`를 삭제하고 같은 수·같은 구성으로 재생성한다.
- `n`은 실행 파라미터로 지정한다. 범위는 `0~100`, 변경 수는 `floor(D × n / 100)`이다.
  `D`는 Dummy 수이며 `n = 0`은 변경 없는 조건이다. 비율과 실제 변경 수를 함께 기록한다.
- 삭제 대상은 고정 seed로 선택하고 세 구현에서 같은 논리적 Dummy를 선택한다.
  대상 선택 계획은 측정 전에 준비한다. Boid 수와 step 종료 시 Dummy 수는 유지한다.

### 공통 상태와 update 순서

동일한 생성 순서, seed, 초기 위치·속도, 파라미터와 fixed timestep을 사용한다.
한 step은 **개체 변경 → 조회 준비 → 이웃 계산 → 상태 갱신** 순서로 수행한다.
이웃 계산은 모든 Boid의 갱신 전 위치·속도를 사용한다. 상태 갱신은 acceleration
반영, 속도 제한·위치 적분, 경계 처리, 최종 velocity에 따른 방향 갱신 순서다.

<a id="experiments"></a>

## 5. 실험 설계와 조건 통제

### 실험 조건

세 구현을 같은 조건에서 비교하는 하나의 벤치마크 실험으로 구성한다.
구현·Boid 수·Dummy 수·변경 비율별로 독립 프로세스를 반복 실행하며, 각 실행에서
전체·네 구간 시간, 초기화 시간과 메모리 지표를 함께 수집한다.

| 변경할 조건 | 값 |
| --- | --- |
| 구현 | OOP / Sparse Set CachedView / Archetype |
| Boid·Dummy 수 | 실행 설정으로 지정 |
| Dummy 삭제·재생성 비율 | `n = 0`(변경 없음)과 지정한 `n > 0` 조건 |

(구체적인 수치와 반복 수는 실행 시간을 테스트해보고 추후에 결정.)

이 결과로 6-1과 6-2의 측정 지표를 측정하게 된다.
6-3의 캐시 측정은 별도의 벤치마킹 시도에서 Vtune같은 외부 프로그램으로 측정한다.

### 조건 통제

- Visual Studio 2022 Release, 동일 compiler·최적화 옵션 사용
- 단일 스레드 headless 실행, benchmark 프로세스 동시 실행 금지

실행 조건(Boid·Dummy 수, 변경 비율, seed, dt, warmup·측정 step 수)과 환경
(CPU·OS·컴파일러·빌드 옵션)을 결과에 함께 기록하고 실행별 원자료를 보존한다.

<a id="metrics"></a>

## 6. 측정 지표와 계측 경계

### 6-1. 시간 지표

| 지표 | 정의 | 별도 계측 필요 여부 | 비고 |
| --- | --- | --- | --- |
| A - 전체 step `ms/step` | 개체 변경부터 상태 갱신 종료까지의 경과 시간을 합산한 뒤 measured steps로 나눈 값 | O | 초기화, warmup과 결과 출력은 전체 step 측정에서 제외한다. |
| B - Updates per second | `1,000 / mean_step_ms`; 단위 시간당 simulation step 처리율 | A로부터 유도 | |
| C - 구간별 `ms/step` | 아래 네 구간 각각의 step당 시간 | O | |
| D - 이웃 Pair당 비용 |  `이웃 계산 ms/step % (N × (N − 1))`; N > 1 | C로부터 유도 | |
| E - 구현 간 시간 비율 | 기준 구현 시간 / 비교 구현 시간; 분자·분모와 통계 방법을 명시 | A나 C로부터 유도 | 같은 실험 조건에서 동일한 지표끼리 계산|
| F- Initialization time | Boid·Dummy와 component를 생성하는 범위를 명시한 초기화 경과 시간 | O | |

| 측정 구간 | 포함할 작업 | 현재 대응 System (Sparse Set ECS 기준) | RQ2에서 설명할 차이 |
| --- | --- | --- | --- |
| 개체 변경 | Dummy 삭제와 재생성 | TODO | 저장 배열의 원소 이동, index 수정, 생성·할당 비용 |
| 조회 준비 | 구조 변경 확인, 대상 목록·index 캐시 갱신 | `BoidSystem` 내부의 캐시 확인·생성·갱신 | 변경 후 다시 데이터를 순회할 준비 비용 |
| 이웃 계산 | 이웃 순회, 거리 검사, Separation·Alignment·Cohesion과 가속도 계산 | `BoidSystem` 내부의 이웃 순회·가속도 계산 | 반복적인 위치·속도 조회에서 나타나는 차이 |
| 상태 갱신 | 속도·위치 갱신, 경계 처리, 방향 갱신 | `MovementSystem` → `BoundarySystem` → `BoidOrientationSystem` | 개체를 순회하며 데이터를 읽고 쓰는 비용 차이 |

조회 준비에는 각 구현에서 실제로 필요한 작업만 포함한다. 별도 준비가 없는 구현에
같은 캐시를 억지로 추가하지 않는다.

전체 시간과 네 구간 시간은 같은 실행에서 측정한다. 전체 시간에는 구간별 계측 비용도
포함되며, 네 구간 계측의 합계가 콜백·디스패치·계측 비용까지 포함한 전체 시간과 같다고 가정하지 않는다.

### 6-2. 메모리 지표

| 지표 | 범위와 한계 | 별도 계측 필요 여부 | 비고 |
| --- | --- | --- | --- |
| G - Peak Private Commit / Peak Working Set | OS가 기록한 프로세스 전체 실행의 최대값 | O (OS 조회) | 시간 측정 종료 후 `PeakPagefileUsage`와 `PeakWorkingSetSize`를 각각 조회한다. 초기화·warmup과 allocator·런타임 비용을 포함하며, 측정 step만의 최대값으로 해석하지 않는다. |
| H - Container size 기반 저장량 | 사용 중인 원소 수 × 원소 크기 및 보조 구조 저장량 | O (size 조회 후 계산) | 측정 종료 시점의 사용 중인 데이터 저장량이다. 프로세스 최대 메모리와 구분하고, allocator overhead·fragmentation은 포함하지 않는다. |
| I - Container capacity 기반 저장량 | 할당된 용량 × 원소 크기 및 보조 구조 저장량 | O (capacity 조회 후 계산) | 측정 종료 시점에 확보한 저장 공간이다. 프로세스 최대 메모리와 구분하고, allocator overhead·fragmentation은 포함하지 않는다. |
| J - Bytes per entity | 선택한 저장량 / 전체 개체 수(Boid + Dummy) | G·H·I로부터 유도 | 프로세스 최대값·사용량·할당량 중 어떤 값을 사용했는지 명시한다. |

컨테이너 추정에는 OOP의 포인터 배열과 개별 객체 저장량, Entity/generation/free list,
sparse/dense 배열, CachedView와 Archetype column을 포함한다. 객체 내부에 포함된 데이터를 중복 계산하지 않는다.
미지원·미측정 값은 실제 0과 구분하여 상태·사유와 함께 저장한다.

### 6-3. 캐시 지표

CPU가 데이터를 읽을 때 L1 캐시에서 찾으면 **L1 hit**, 찾지 못하면 **L1 miss**다.
L3 miss는 L3 캐시에서도 찾지 못한 경우다. 같은 시뮬레이션 조건에서 세 구현의
캐시 사용 차이를 살펴보는 보조 근거로 사용한다.

| 지표 | 정의 | 별도 계측 필요 여부 | 비고 |
| --- | --- | --- | --- |
| K - L1 hit·miss 추정 횟수 | 데이터를 읽을 때 L1 캐시에서 찾은 횟수와 찾지 못한 횟수의 추정값 | O (VTune) | `MEM_LOAD_RETIRED.L1_HIT`와 `MEM_LOAD_RETIRED.L1_MISS`를 각각 수집한다. |
| L - L1 miss 비율 | `L1 miss / (L1 hit + L1 miss) × 100` (%) | K로부터 유도 | 샘플 수가 아닌 추정 횟수로 계산한다. 분모가 0이면 계산하지 않는다. |
| M - L3 miss 추정 횟수 | 데이터를 읽을 때 L3 캐시에서도 찾지 못한 횟수의 추정값 | O (VTune) | `MEM_LOAD_RETIRED.L3_MISS`를 수집한다. L3 hit는 수집 대상이 아니므로 L3 miss 비율은 계산하지 않는다. |

VTune은 모든 hit·miss를 하나씩 기록하는 대신, 일정 횟수마다 **샘플**을 기록한다.
샘플 수와 샘플링 간격을 반영한 **추정 횟수**를 구분한다. 예를 들어 hit는 1,000회마다,
miss는 100회마다 기록했다면 두 샘플 수를 그대로 나눠 miss 비율을 구할 수 없다.
추정 횟수는 VTune의 `Hardware Event Count`, 샘플 수는 `Hardware Event Sample Count`를
사용한다. [VTune 지표 설명](https://www.intel.com/content/www/us/en/docs/vtune-profiler/user-guide/2023-0/window-summary-hardware-events.html)

캐시 측정은 일반 시간·메모리 벤치마크와 별도로 실행한다.

<a id="analysis"></a>

## 7. 통계 분석과 결과 해석

통계 단위는 **독립 프로세스 한 번의 평균 step 시간**이다. 한 실행에서
측정한 1,000 step을 독립 반복 1,000회로 계산하지 않는다. 조건은 구현·N·seed·
Dummy 수·변경 비율·설정·horizon으로 구분한다.

조건별 median, IQR, mean, standard deviation, min/max와 유효 반복 수를 보고한다.
최소 9회, 권장 12회 반복을 확보하고 변동이 크면 근거를 남겨 확대한다.
구현 간 시간 차이 또는 비율에는 신뢰구간을 제시한다. 짝지은 비교라면 같은
반복 블록을 대응시키고, 신뢰수준·계산 방법·bootstrap 등의 난수 seed를 기록한다.

이상치는 사전에 정한 기준으로 표시하고 제외 여부·근거·제외 전후 결과를 남긴다.
최솟값이나 best-of-N만으로 주 결론을 내리지 않는다. 실패·상태 불일치·조건
누락은 분석 오류로 드러나야 하며 분석기가 조용히 무시하게 하지 않는다.

필수 분석은 다음과 같다.

- RQ1: Boid 수·Dummy 수·변경 비율에 따른 전체 `ms/step`, 산포와 구현 간 비율·신뢰구간
- RQ2: 네 구간의 비용과 비중, pair당 이웃 비용, 일반 View/CachedView 보조 비교
- RQ3: 초기화 시간, 프로세스 메모리, size/capacity 저장량과 bytes per entity
- 재현성: 반복 누락·실패·이상치와 대표 조건 재실행의 안정성

phase 차이와 코드 경로는 접근 비용을 설명하는 근거지만 캐시 인과관계를
단독으로 증명하지 않는다. OOP와 Archetype의 차이가 작아도 동등성을 주장하려면
별도의 동등성 기준과 분석이 필요하다. 결과의 적용 범위는 정의한 Boid·Dummy 구성과
삭제·재생성 조건으로 제한한다. 컴포넌트 제거·재부착이나 다른 ECS 라이브러리의
우열로 일반화하지 않는다.

<a id="paper"></a>

## 8. 논문 구성과 필요한 표·그림

| 장 | 작성 내용 | 필요한 근거·산출물 |
| --- | --- | --- |
| 1. 서론 | 연구 배경, 문제 정의, 선행연구와 차별점, RQ와 기여 | 연구 범위·RQ 요약 |
| 2. 이론적 배경 | OOP 상속·객체 소유·포인터 접근, ECS·DOD, Sparse Set·query, Archetype column, 3D Boids·naive 탐색 | Reynolds·Cox 참고문헌, 개념별 저장 배치 그림 |
| 3. 설계 및 구현 | 경량 runtime·headless 경로, 세 구현의 데이터·접근 방식, 공통 네 구간과 동등성 | 모듈/실행 흐름 그림, 세 저장 구조 비교표, 테스트 조건·오차 |
| 4. 실험 방법 | 환경·빌드, Boid·Dummy 수·변경 비율·seed·dt·horizon, 독립 반복, 시간·초기화·메모리·캐시 범위, 통계·실패 처리 | 환경표, 조건표, 지표 정의표, 재현 명령 |
| 5. 결과 | 전체·네 구간 시간, 변경 비율별 차이, 초기화·메모리, 보조 비교·지원 캐시 지표 | 원자료와 연결된 통계표·그래프 |
| 6. 논의 | RQ별 해석, OOP/Archetype 관계, View/CachedView와 index 비용, 적용 조건·인과 한계 | 효과 크기·신뢰구간, 코드 경로와 계측 근거 |
| 7. 한계 | 단일 CPU·Windows·MSVC, 단일 스레드·Boid/Dummy 두 구성, 컴포넌트 제거·재부착 미평가, CPU headless 범위 | 일반화 범위와 미측정 지표·제약 |
| 8. 결론 | RQ1~RQ3 답변, 시간·메모리 trade-off, workload별 선택 시사점·후속 연구 | 검증된 결과만 요약 |

필수 표·그림은 다음과 같다.

1. 환경·compiler·빌드·통제 조건표와 실험 행렬
2. 세 구현의 데이터 배치·접근 경로 그림 및 공통 update 흐름
3. Boid·Dummy 수와 변경 비율에 따른 전체 step 시간 그래프: median과 IQR 등 산포 표시
4. 구현 간 시간 차이 또는 비율과 신뢰구간 표·그래프
5. 네 구간의 `ms/step`·비중 및 pair당 이웃 비용
6. 초기화 시간, 프로세스·컨테이너 메모리와 bytes per entity
7. 일반 View/CachedView 보조 결과, 캐시 결과표 또는 지원 제약표
8. 동등성 검증 범위와 반복 수·실패·이상치 처리 요약

모든 표·그림에 단위, 조건, 유효 반복 수와 원자료 run ID를 연결한다.
논문에는 구현 완료 후 수행한 측정 결과를 사용한다. 서론·배경·설계·방법은
구현과 함께 작성하고, 결과·논의·결론은 최종 분석 이후 수치를 확정한다.
학교의 제출 형식·분량·마감은 확인 후 이 문서에 반영한다.

<a id="completion"></a>

## 9. 최종 검증·제출 기준

졸업 연구의 최종 결과물은 다음 기준을 충족해야 한다.

### 구현·정확성

- 세 구현이 동일한 Boid·Dummy 구성과 삭제·재생성 조건 및 naive all-pairs 사용
- 후보 최적화·일반 View 보조 경로와 최종 기본 경로를 구분함
- VS2022 configure, Debug/Release 빌드·전체 테스트 및 세 CLI smoke 통과
- 최종 CMake의 clangd database 갱신
- 여러 seed·경계·퇴화·Dummy 변경 조건과 기본/확장 horizon에서 매 step 상태 동등성·허용 오차 검증
- OOP의 ECS 비의존성, RenderData와 App 책임 경계 유지

### 실험·재현성

- 코드·빌드·설정을 동결하고 commit/dirty/source/executable hash 기록
- 100/500/1000/3000의 같은 조건에서 조건별 최소 9회, 권장 12회 독립 반복
- 5000/10000 확장 지원과 실시 여부·제외 사유 기록
- 구현별 실행 순서 교차, 순차 실행, 동일 조건·horizon 유지
- 전체·네 구간 시간과 메모리를 같은 실행에서 수집하고 측정 범위·계측 비용 명시
- 캐시 자료 또는 지원 제약 보고서 연결; 미측정 캐시 인과 주장 없음
- 실행별 원자료·환경 metadata·실패 로그 보존, checksum·조건·스키마 검증
- median/IQR/mean/SD/min/max, 차이 또는 비율의 신뢰구간과 유효 반복 수 산출
- 실패·이상치·재개·중복 처리 기준 기록
- 원자료만으로 표·그래프 재생성 및 대표 조건 재실행 안정성 확인
- 실행·측정·분석 명령과 원자료 위치를 함께 보존

### 논문·제출물

- RQ1~RQ3에 최종 시간·phase·초기화·메모리 결과로 답변
- 일반 View/CachedView 보조 결과와 최종 세 구조 비교의 범위 명시
- 단일 CPU·단일 스레드·Boid/Dummy 구성과 변경 조건 및 인과 해석 한계 명시
- 구현 완료 후 측정한 결과만 사용하고 모든 표·그림의 원자료·단위·반복 수 대조
- 참고문헌 서지·인용과 학교 형식·분량·제출 요건 확인
- README·architecture·tech-stack 및 이 문서의 연구·설계 기준 정합성 확인
- 실행 가능한 런타임·headless 3D Boids 시뮬레이션, 최종 소스 commit, 재현 자료와 논문 완성본 보존
- 팀원이 같은 환경에서 실행·재현 절차를 확인하고 제출본을 함께 검토

<a id="references"></a>

## 10. 참고문헌과 근거 자료

- Reynolds, C. W. (1987). *Flocks, Herds, and Schools: A Distributed Behavioral
  Model*. Computer Graphics, 21(4), 25–34.
  [저자 제공 원전](https://www.red3d.com/cwr/papers/1987/boids.html)
- Cox, L., Williams, B., Vickers, J., Ward, D., & Headleand, C. (2025).
  *Run-time Performance Comparison of Sparse-set and Archetype Entity-Component
  Systems*. Computer Graphics and Visual Computing (CGVC), The Eurographics Association.
  [출판사 자료](https://diglib.eg.org/items/6e291ae6-e32c-4c21-a89b-021fd9986ede),
  [DOI](https://doi.org/10.2312/cgvc.20251224)
