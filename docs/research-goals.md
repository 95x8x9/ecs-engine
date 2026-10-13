# 졸업 논문 통합 계획

갱신일: 2026-10-13

구현 진행 순서는 [GitHub 이슈 #30](https://github.com/learncold/ecs-engine/issues/30)을
따른다. 진행 상태와 완료 여부는 GitHub 이슈에서 관리한다.

## 목차

1. [연구 주제와 목표](#research)
2. [연구 질문과 가설](#questions)
3. [범위와 아키텍처 원칙](#scope)
4. [비교 구현과 공통 시뮬레이션 규칙](#implementations)
5. [실험 설계와 조건 통제](#experiments)
6. [측정 지표와 계측 경계](#metrics)
7. [반복 실행·원자료·재현 절차](#reproduction)
8. [통계 분석과 결과 해석](#analysis)
9. [구현 로드맵과 단계별 수용 기준](#roadmap)
10. [논문 구성과 필요한 표·그림](#paper)
11. [팀 협업과 문서 갱신 규칙](#team)
12. [최종 완료·제출 체크리스트](#completion)
13. [참고문헌과 근거 자료](#references)

<a id="research"></a>

## 1. 연구 주제와 목표

### 논문 제목

국문: **Boids 시뮬레이션에서 OOP, Sparse Set ECS 및 Archetype ECS의 성능 비교**

영문: **A Performance Comparison of OOP, Sparse Set ECS, and Archetype ECS
in a Boids Simulation**

### 배경과 연구 목적

ECS(Entity-Component-System)의 실행 비용은 컴포넌트 저장 방식과 반복 접근
경로에 따라 달라진다. Sparse Set은 타입별 저장소와 sparse/dense index를 사용하고,
Archetype은 동일한 컴포넌트 조합을 column으로 저장한다. 연속 객체 배열 OOP도
동종 데이터를 처리할 때 높은 지역성을 가질 수 있다.

동일한 단일 스레드 3D Boids Simulation을 위 세 구조로 구현하고, 알고리즘과 초기
상태를 통제하여 성능을 정량적으로 분석한다. 특정 구조가 항상
빠르다는 결론을 전제하지 않는다. 관측된 차이를 데이터 배치, 쿼리·index 접근과
phase별 처리 비용 관점에서 설명한다.

연구 목표는 다음과 같다.

1. 경량 ECS 런타임과 headless로 실행 가능한 3D Boids 시뮬레이션을 완성한다.
2. OOP, Sparse Set ECS, Archetype ECS로 동일한 시뮬레이션을 구현·검증한다.
3. 개체 수에 따른 전체 및 phase별 update 성능을 비교한다.
4. Sparse Set CachedView와 Archetype 직접 column 순회의 접근 비용을 분석한다.
5. 초기화 시간과 프로세스·컨테이너 메모리를 비교한다.
6. 원자료로 재생성 가능한 표·그래프와 RQ(Research Question)별 결론을 졸업 논문으로 제시한다.

### 선행연구와 기여

[Reynolds(1987)](https://www.red3d.com/cwr/papers/1987/boids.html)는 개별 개체의
지역적 행동으로 군집 움직임을 만드는 Boids 모델의 출발점이다. 본 구현은
그 모델에서 착안한 단순화된 3D 실험 시뮬레이션이며 원전의 모든 기능을 재현하지 않는다.

[Cox 등(2025)](https://diglib.eg.org/items/6e291ae6-e32c-4c21-a89b-021fd9986ede)은
Sparse Set과 Archetype ECS의 순회 및 개체 구성 변경 비용을 비교한다.
본 연구는 3D Boids에서 연속 객체 배열 OOP를 비교 기준으로 추가하고,
CachedView 적용 전후의 보조 비교, phase별 시간, 초기화와 메모리를 함께 분석한다.
기존 연구의 결론을 그대로 재현한다고 가정하지 않는다.

<a id="questions"></a>

## 2. 연구 질문

| 연구 질문 | 필요한 근거 | 논문에서 답할 내용 |
| --- | --- | --- |
| RQ1. 세 구조의 전체 update 성능은 개체 수에 따라 어떻게 달라지는가? | 동일 조건의 전체 `ms/step`, 산포, 비율과 신뢰구간 | 개체 수별 차이와 scaling |
| RQ2. 저장 구조와 반복 접근 경로는 관측된 성능 차이와 어떤 관계가 있는가? | phase별 시간, 구현 경로, View/CachedView 보조 실험, 가능한 캐시 측정 | 차이를 만드는 구간과 원인 해석의 한계 |
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
| `Boids/OOP/` | 객체 배열 시뮬레이션; `Engine/ECS`에 의존하지 않음 |
| `Boids/ECS/` 및 Archetype 구현 | ECS 기반 저장·시스템과 연구 도메인 행동 |
| `App/` | 구현 선택, 콜백 연결, 옵션·출력을 담당하는 얇은 조립 계층 |

`Engine`에 Boids 행동을 넣지 않는다. `App`에 ECS 쿼리나 Boids 시스템을 구현하지
않는다. 위치·회전·크기 데이터와 관련 계산 코드는 OpenGL 렌더링 코드와 분리한다. 시뮬레이션에서는 GLM 수학 라이브러리만 사용하는 Engine::RenderData Cmake 빌드 타겟을 통해 이를 사용한다. 시뮬레이션에 OpenGL 실행 의존성을 추가하지 않는다.
헤더와 소스는 기능별 폴더에 함께 둔다.

### 제외 범위

렌더링·시각화의 추가 구현과 FPS·frame time·render CPU/GPU time 측정은 프로젝트
완료 범위와 논문에서 제외한다. 3D는 위치·속도·행동 계산의 공간 차원을 뜻하며
화면 출력은 요구하지 않는다.

<a id="implementations"></a>

## 4. 비교 구현과 공통 시뮬레이션 규칙

### 최종 비교 경로

| 구현 | 저장 방식 | 주요 접근 경로 |
| --- | --- | --- |
| OOP | 연속 `std::vector<BoidObject>` | 객체 배열 직접 순회 |
| Sparse Set ECS | 타입별 sparse index, dense entity·component 배열 | 이웃 `CachedView`의 Entity·저장소별 dense index 재사용 |
| Archetype ECS | 단일 고정 component 조합의 column 배열 | column과 행 범위를 얻어 직접 순회 |

OOP 객체는 Transform, MeshRenderer, velocity, acceleration과 행동 파라미터를
소유한다. 상속, 가상 함수와 개체별 heap 할당을 의도적으로 넣어 기준선을
느리게 만들지 않는다.

Sparse Set의 이웃 CachedView에는 `Transform + Velocity`가 사용된다. 이웃
반복에서 sparse 조회는 제거되지만 캐시된 dense index 접근은 남는다. 외부
개체 순회와 다른 phase의 일반 View 비용까지 사라지는 것은 아니다.
일반 View는 보조 실험, CachedView는 주 비교 경로로 구분한다.

CachedView는 matching Entity와 저장소별 dense index를 보관한다. 값 변경은
최신 component 값을 읽으며, 구조 변경은 revision/epoch 등으로 감지하여 다음
사용 전에 갱신한다. Create/Destroy/Emplace/Remove/Clear, 다른 Registry로의
사용과 Registry 수명에 대한 규칙을 검증한다. 순회 콜백 안에서 구조를 변경하지 않는다.

Archetype의 평가 대상은 다음 고정 시그니처다.

```text
Boid Archetype
├─ Entity[]
├─ Transform[]
├─ MeshRenderer[]
├─ Velocity[]
├─ Acceleration[]
└─ BoidParameters[]
```

동종 개체 하나의 안정된 구조를 평가하며, 구성 변경·migration이나 범용 query
planner의 성능을 평가했다고 쓰지 않는다.

### 공통 상태와 update 순서

동일한 생성 순서, seed, 초기 위치·속도, 파라미터와 fixed timestep을 사용한다.
모든 개체가 같은 step 시작 상태를 기준으로 이웃을 계산하도록 다음 순서를 유지한다.

1. 모든 개체의 이웃을 순회하여 Separation·Alignment·Cohesion과 acceleration 계산
2. acceleration을 velocity에 반영하고 속도를 제한한 뒤 position 적분
3. 경계 처리
4. 최종 velocity에 맞춘 orientation 갱신

이웃 탐색은 세 구현 모두 자기 자신을 제외한 naive all-pairs다. 거리 계산은
step당 `N × (N − 1)`회이며 시간 복잡도는 `O(N²)`다. 이웃으로 채택되는 개체 수와
거리 검사 횟수를 구분한다. 반경·경계·밀도도 workload의 일부로 고정한다.

`max_alignment_force`는 가중치 적용 전 Alignment 조향력만 제한한다.
Separation·Cohesion과 합산 acceleration의 상한으로 해석하지 않는다.

현재 `BoidSimulationConfig` 기본값은 다음과 같다. 최종 실험에서는 기본값에
의존해 생략하지 말고 실제 사용값을 설정 파일과 metadata에 저장한다.

| 설정 | 값 |
| --- | ---: |
| agent count / seed | 100 / 42 |
| spawn half extent / boundary half extent | 10 / 25 |
| initial speed / preferred speed / max speed | 4 / 4 / 6 |
| neighbor radius / separation radius | 6 / 2.5 |
| separation / alignment / cohesion weight | 2.5 / 1 / 0.2 |
| max alignment force | 1.5 |
| benchmark dt / warmup / measured steps | `1/60` seconds / 100 / 1,000 |

### 정확성 검증

초기 상태와 매 step의 위치·속도·가속도·회전·크기를 동일 개체끼리 비교한다.
여러 seed, 경계 반사, 겹친 위치, 속도 0, `dt = 0`과 최종 기본·확장 horizon을
포함한다. `dt = 0`은 정확성 테스트 조건이며 양수 dt가 필요한 CLI benchmark와
구분한다. 허용 절대·상대 오차와 회전 비교 기준을 기록한다.

checksum은 실행 결과의 일치 확인에 사용하되, 폭넓은 상태 동등성 검증을
대체하지 않는다. 계측 켜기·끄기도 결과를 바꾸지 않아야 한다.

<a id="experiments"></a>

## 5. 실험 설계와 조건 통제

### 실험 행렬

| 구분 | 조건 | 목적 |
| --- | --- | --- |
| 주 실험 | OOP / Sparse CachedView / Archetype × naive all-pairs | RQ1의 전체 update·scaling 비교 |
| Phase 실험 | 같은 세 구현과 workload, phase 계측 활성화 | RQ2의 구간별 비용 분석 |
| 초기화·메모리 | 같은 N과 생성 데이터, 계측 범위 명시 | RQ3의 생성·저장 비용 비교 |
| 보조 실험 | 일반 View / CachedView, 동일 기반 코드 | index 조회 제거 효과 확인 |
| 캐시 프로파일링 | 동일 조건, 일반 시간 측정과 별도 실행 | 접근 비용 해석의 추가 근거 또는 지원 제약 보고 |

기본 개체 수는 **100, 500, 1,000, 3,000**이다. **5,000과 10,000**도 확장
조건으로 지원하되 실행 시간과 자원에 따라 실시 여부·제외 사유를 기록한다.

최종 warmup·measured steps는 예비 실행으로 실행 시간을 확인한 후 동결한다.
현재 CLI의 100/1,000 기본값은 최종 실험의 확정값이 아니다.
개체 수에 따라 horizon을 달리 정하면 이유와 값을 남기고, 같은 N의 세 구현에는
반드시 동일한 seed·dt·파라미터·warmup·measured steps를 적용한다.

### 실행 통제

- Visual Studio 2022 Release, 동일 compiler·최적화 옵션 사용
- 단일 스레드 headless 실행, benchmark 프로세스 동시 실행 금지
- 조건별 최소 9회, 권장 12회 독립 프로세스 반복; 매번 초기화와 warmup 수행
- 반복마다 구현 순서를 회전·교차하여 시간대·발열·전력 변화의 편향 완화
- CPU·OS·compiler·빌드 설정, 전원 모드·부하·우선순위·affinity 적용 여부 기록
- 코드 commit, dirty state, 변경 소스 해시와 executable hash 보존
- P-core 사용을 주장할 경우 CPU 토폴로지와 논리 프로세서 매핑 확인
- 실패·중단·이상치를 임의 삭제하지 않고 사전에 정한 처리 기준 적용

전체 시간, phase 계측, 외부 profiler 실행은 별도 측정 모드로 분리한다.
계측 비용을 확인하고 조건·모드가 다른 결과의 절대 시간을 직접 합치지 않는다.

<a id="metrics"></a>

## 6. 측정 지표와 계측 경계

### 시간 지표

| 지표 | 정의와 보고 기준 |
| --- | --- |
| 전체 update `ms/step` | 실제 `BoidSimulation::Update` 콜백 전후의 경과 시간을 합산한 뒤 measured steps로 나눈 값 |
| Updates per second | `1,000 / mean_update_ms`; 단위 시간당 simulation step 처리율 |
| Phase `ms/step` | 이웃·acceleration / movement / boundary / orientation 각 구간의 step당 시간 |
| Pair당 이웃 비용 | 이웃 phase 총 시간을 `measured_steps × N × (N − 1)`로 나눔; N ≤ 1은 적용 불가 |
| 구현 간 시간 비율 | 기준 구현 시간 / 비교 구현 시간; 분자·분모와 통계 방법을 명시 |
| Initialization time | N개 개체·component 생성 범위를 명시한 초기화 경과 시간 |

전체 Update 타이머에는 콜백 호출과 실행 중 스케줄링 지연이 포함된다.
CPU 사용 시간이 아니다. 초기화, warmup과 결과 출력은 주 update 측정에서 제외한다.
Fixed dt는 가상 시간의 진행량이며 실제 실행 속도를 제한하지 않는다.

전체 타이머와 phase 타이머는 별도 실행한다. Phase 합계가 콜백·디스패치·계측
비용까지 포함한 전체 시간과 같다고 가정하지 않는다. 초기화 timer의 시작·끝,
`reserve` 포함 여부와 lazy CachedView 생성 시점을 기록한다. 첫 warmup에서
구성되는 캐시 비용도 생성·최초 사용 비용으로 별도 식별하여 누락하지 않는다.

### 메모리 지표

| 지표 | 범위와 한계 |
| --- | --- |
| Process Private Commit / Working Set | 각각의 OS 지표를 구분; allocator·런타임 등 프로세스 전체 비용 포함 |
| Container size 기반 저장량 | 사용 중인 원소 수 × 원소 크기 및 보조 구조 저장량 |
| Container capacity 기반 저장량 | 할당된 용량 × 원소 크기 및 보조 구조 저장량 |
| Bytes per entity | 선택한 저장량 / N; 사용량·할당량·프로세스 기준을 명시 |

초기화 전·후, warmup 후, 측정 종료 등 정해진 시점에 Update timer 밖에서
프로세스 메모리를 수집한다. 시점별 snapshot과 peak를 구분하며 snapshot을
peak라고 보고하지 않는다.

컨테이너 추정에는 Entity/generation/free list, sparse/dense 배열, CachedView와
Archetype column을 포함한다. 객체 내부에 포함된 데이터를 중복 계산하지 않는다.
allocator overhead·fragmentation 등 포함하지 못한 범위를 명시한다.
미지원·미측정 값은 실제 0과 구분하여 상태·사유와 함께 저장한다.

### 캐시와 CPU 보조 지표

P-core의 앱 귀속 L1 load miss 비율과 pair당 miss 지표를 우선 확인하고 L3는
보조 지표로 둔다. VTune 등 도구에서 이벤트를 지원할 때만 의미·분모·집계 구간을
확인한 후 `L1_MISS / (L1_HIT + L1_MISS)` 근사 비율을 사용할 수 있다.

샘플 수, 추정 이벤트 수와 실제 miss 횟수를 혼동하지 않는다. Pair당 지표는
실제 포함된 step 수와 `N × (N − 1)`을 분모로 하고 초기화·warmup 포함 여부를
명시한다. 도구 버전, 권한·드라이버, 지원 이벤트, 표본 수와 계측 부담을 기록한다.

신뢰할 만한 카운터를 얻지 못하면 지원 제약 보고서를 남긴다. 캐시 우위를
성능 차이의 원인으로 확정하거나 시간으로 miss 값을 만들어 채우지 않는다.
프로세스 CPU 시간과 cycles는 선택 지표이며 추가 시 경과 시간과 구간을 구분한다.

<a id="reproduction"></a>

## 7. 반복 실행·원자료·재현 절차

### 현재 사용 가능한 빌드·실행 명령

저장소 루트에서 실행한다. Debug는 기능 검증, Release는 성능 측정에 사용한다.

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
cmake --build --preset vs2022-release
ctest --test-dir build/vs2022 -C Release --output-on-failure
```

소스 배치, CMake 목록·include 경로·의존성을 바꾸면 다음도 실행한다.

```powershell
cmake --preset clangd
```

연구 실행에는 `--benchmark`를 사용한다. 현재 CLI는 `ecs`와 `oop`를 지원하며,
아래 값은 실행 예시다.

```powershell
.\build\vs2022\App\Release\App.exe --help
.\build\vs2022\App\Release\App.exe --benchmark --implementation ecs --agents 100 --seed 42 --warmup 100 --steps 1000 --dt 0.016666667
.\build\vs2022\App\Release\App.exe --benchmark --implementation oop --agents 100 --seed 42 --warmup 100 --steps 1000 --dt 0.016666667
```

`archetype` 선택, 공통 기계 판독 출력, runner와 analyzer는 후속 통합 대상이다.
통합 후 실제 동작하는 실행·분석 명령과 결과 경로를 이 절에 추가한다.
검증되지 않은 예정 명령을 현재 사용 가능한 명령으로 적지 않는다.

### 최종 실행 절차

1. 최종 기준 commit과 실험 설정을 동결하고 코드·소스·실행파일 해시를 기록한다.
2. Debug/Release 전체 테스트, 세 CLI smoke와 최종 horizon 상태 동등성을 확인한다.
3. 별도 run ID로 조건·실행 순서를 확정하고 최소 9회 독립 프로세스로 측정한다.
4. 전체 시간·phase 모드를 분리하고 초기화·메모리와 환경 metadata를 수집한다.
5. 원자료 스키마, 단위, 조건 일치, 누락·중복, status와 checksum을 검사한다.
6. 저장한 원자료만으로 통계·신뢰구간·표·그래프를 생성한다.
7. 캐시 profiler는 동일 조건의 별도 실행으로 수집하거나 지원 제약을 보고한다.
8. 대표 조건을 다시 실행한다. 큰 편차는 사유와 추가 반복을 기록한다.
9. 논문 표·그림의 숫자를 원자료·분석 출력과 대조한다.

### 실행별 데이터와 보존 규칙

runner는 공통 계측 출력을 받아 실행별 CSV를 생성한다. 다음은 최소 필드
계획이며 최종 구현 시 각 필드의 단위·정의와 schema version을 확정한다.

```text
schema_version, run_id, implementation, agent_count, seed, dt,
parameters, spawn_half_extent, boundary_half_extent, initial_speed,
warmup_steps, measured_steps, repetition, execution_order, measurement_mode,
total_update_ms, mean_update_ms, updates_per_second,
neighbor_ms, movement_ms, boundary_ms, orientation_ms,
initialization_ms, cache_setup_ms,
process_private_commit_bytes, process_working_set_bytes, memory_snapshot_stage,
container_size_bytes, container_capacity_bytes, bytes_per_entity,
checksum, status, failure_reason,
commit, dirty, source_hash, executable_hash, cpu, os, compiler, build_options,
affinity, priority, power_mode
```

파라미터·환경은 run별 metadata 파일로 분리해도 되지만 CSV의 run ID로
연결되어야 한다. 시간 필드가 실행 합계인지 step당 평균인지 명시한다.
메모리 snapshot이 여러 개면 별도 행·파일로 stage를 보존한다.

원자료, stdout/stderr, 실패·중단 로그, 설정, profiler 원본, 분석 스크립트·명령과
생성물을 함께 보존한다. 재개·재실행은 기존 자료를 덮어쓰지 않고 새 ID로
구분하여 중복 집계를 막는다. 프로세스 실패와 상태 불일치는 성공 관측값에
섞지 않는다. 코드나 조건 변경 후 결과를 같은 배치로 취급하지 않는다.

최종 재현 기록에는 기준 commit, 환경, correctness 근거, 설정 파일,
실행·분석 명령, 원자료 위치와 생성된 표·그림 위치를 반드시 채운다.
생성된 `build/`는 커밋하지 않는다.

### 최종 재현 기록 양식

아래 값은 구현 완료 후 최종 실험 동결 시 채운다.
경로·명령이 길면 이 절에 항목별로 이어서 기록한다.

| 기록 항목 | 최종 확정값·근거 |
| --- | --- |
| 기준 commit / dirty / source·executable hash | 미확정 |
| CPU·OS·compiler·빌드 옵션·전원·affinity | 미확정 |
| 설정 파일 / seed·dt·행동 파라미터·공간 조건 | 미확정 |
| N별 warmup·measured steps / 모드별 독립 반복 수 | 미확정 |
| 정확성 테스트 조건·오차 / 빌드·테스트 로그 | 미확정 |
| runner 실행 명령 / run ID / 원자료·실패 로그 위치 | 미확정 |
| analyzer 명령 / 신뢰구간 방법·seed / 표·그림 위치 | 미확정 |
| profiler 명령 / 캐시 자료 또는 지원 제약 보고 위치 | 미확정 |
| 대표 조건 재실행 기록 | 미확정 |

<a id="analysis"></a>

## 8. 통계 분석과 결과 해석

통계 단위는 **독립 프로세스 한 번의 평균 update 시간**이다. 한 실행에서
측정한 1,000 step을 독립 반복 1,000회로 계산하지 않는다. 조건은 구현·N·seed·
설정·horizon·측정 모드로 구분한다.

조건별 median, IQR, mean, standard deviation, min/max와 유효 반복 수를 보고한다.
최소 9회, 권장 12회 반복을 확보하고 변동이 크면 근거를 남겨 확대한다.
구현 간 시간 차이 또는 비율에는 신뢰구간을 제시한다. 짝지은 비교라면 같은
반복 블록을 대응시키고, 신뢰수준·계산 방법·bootstrap 등의 난수 seed를 기록한다.

이상치는 사전에 정한 기준으로 표시하고 제외 여부·근거·제외 전후 결과를 남긴다.
최솟값이나 best-of-N만으로 주 결론을 내리지 않는다. 실패·상태 불일치·조건
누락은 분석 오류로 드러나야 하며 분석기가 조용히 무시하게 하지 않는다.

필수 분석은 다음과 같다.

- RQ1: 전체 `ms/step` 대 N, 산포와 구현 간 비율·신뢰구간
- RQ2: phase별 비용과 비중, pair당 이웃 비용, 일반 View/CachedView 보조 비교
- RQ3: 초기화 시간, 프로세스 메모리, size/capacity 저장량과 bytes per entity
- 재현성: 반복 누락·실패·이상치와 대표 조건 재실행의 안정성

phase 차이와 코드 경로는 접근 비용을 설명하는 근거지만 캐시 인과관계를
단독으로 증명하지 않는다. OOP와 Archetype의 차이가 작아도 동등성을 주장하려면
별도의 동등성 기준과 분석이 필요하다. 정적 동종 Boids의 결과로 구성 변경이
많은 상황이나 다른 ECS 라이브러리의 우열을 결론 내리지 않는다.

<a id="roadmap"></a>

## 9. 구현 로드맵과 단계별 수용 기준

아래 순서는 #30의 구현 체크리스트와 같다. 진행 상태는 연결된 GitHub 이슈에서 확인한다.
각 단계는 수용 기준을 충족한 코드가 기준 브랜치에 반영되고 검증 근거가 있어야
완료로 판정한다. 최종 전체 데이터 수집은 마지막 통합 검증 단계에서 수행한다.

| 순서 | 이슈 | 수용 기준 요약 |
| ---: | --- | --- |
| 1 | [#26 Registry Clear 수명](https://github.com/learncold/ecs-engine/issues/26) | generation 이력 유지, 여러 Entity·반복 Clear/재생성 및 오래된 핸들의 IsAlive/Destroy/Get 회귀 검증, ID 재사용·캐시 무효화 유지 |
| 2 | [#27 CachedView 확정](https://github.com/learncold/ecs-engine/issues/27) | Entity·dense index 캐시 기준 코드 반영, 값/구조 변경과 다른 Registry 사용 규칙 검증, BoidSystem 재사용, OOP/Sparse 동등성·문서 일치 |
| 3 | [#28 Archetype 통합](https://github.com/learncold/ecs-engine/issues/28) | 실제 App의 `oop\|ecs\|archetype` headless 선택, 같은 초기 상태·phase 순서, 경계·겹침·0속도·0dt·최종 horizon 상태 비교, 모듈 경계 유지 |
| 4 | [#29 공통 계측](https://github.com/learncold/ecs-engine/issues/29) | 실제 세 Update의 전체/phase 모드 분리, 초기화·lazy cache 비용과 메모리 범위 정의, 동일 기계 판독 스키마, 계측 on/off 정확성·미지원 표시 |
| 5 | [#13 반복 자동화·분석](https://github.com/learncold/ecs-engine/issues/13) | 조건별 독립 프로세스·순서 교차, CSV·해시·metadata·실패 로그 보존, 누락/중복/동등성 검사, 원자료 기반 통계·신뢰구간·그래프 재생성 |
| 6 | [#22 캐시 측정·한계](https://github.com/learncold/ecs-engine/issues/22) | 같은 workload의 실제 경로 프로파일링, P-core 매핑·이벤트·샘플·분모·오버헤드 확인, 결과표 또는 근거 있는 지원 제약 보고서 |
| 7 | [#30 최종 통합·전체 측정](https://github.com/learncold/ecs-engine/issues/30) | 하나의 동결 commit, 전체 검증, 기본 조건별 9회 이상 측정, 원자료·통계·재실행·문서와 RQ 결과 연결 |

관련 단계마다 VS2022 Debug/Release 빌드·테스트를 확인한다. CMake나 파일 배치가
바뀌면 clangd database도 갱신한다. 기존 Benchmarker와 Update 경로를 재사용한다.

#13의 기술적 선행은 #29이며 #22는 #13 및 #29 이후에 진행한다. 캐시 카운터가
장치에서 미지원이어도 근거·해석 제한을 남기면 #22를 판정할 수 있지만,
초기화·메모리 기본 계측은 별도로 완료해야 한다.

<a id="paper"></a>

## 10. 논문 구성과 필요한 표·그림

| 장 | 작성 내용 | 필요한 근거·산출물 |
| --- | --- | --- |
| 1. 서론 | 연구 배경, 문제 정의, 선행연구와 차별점, RQ와 기여 | 연구 범위·RQ 요약 |
| 2. 이론적 배경 | OOP 객체 배열·AoS, ECS·DOD, Sparse Set·query, Archetype column, 3D Boids·naive 탐색 | Reynolds·Cox 참고문헌, 개념별 저장 배치 그림 |
| 3. 설계 및 구현 | 경량 runtime·headless 경로, 세 구현의 데이터·접근 방식, 공통 phase와 동등성 | 모듈/실행 흐름 그림, 세 저장 구조 비교표, 테스트 조건·오차 |
| 4. 실험 방법 | 환경·빌드, N·seed·dt·horizon, 독립 반복, 시간·초기화·메모리·캐시 범위, 통계·실패 처리 | 환경표, 조건표, 지표 정의표, 재현 명령 |
| 5. 결과 | 전체·phase 시간, 초기화·메모리, scaling, 보조 비교·지원 캐시 지표 | 원자료와 연결된 통계표·그래프 |
| 6. 논의 | RQ별 해석, OOP/Archetype 관계, View/CachedView와 index 비용, 적용 조건·인과 한계 | 효과 크기·신뢰구간, 코드 경로와 계측 근거 |
| 7. 한계 | 단일 CPU·Windows·MSVC, 단일 스레드·동종 workload, migration 미평가, CPU headless 범위 | 일반화 범위와 미측정 지표·제약 |
| 8. 결론 | RQ1~RQ3 답변, 시간·메모리 trade-off, workload별 선택 시사점·후속 연구 | 검증된 결과만 요약 |

필수 표·그림은 다음과 같다.

1. 환경·compiler·빌드·통제 조건표와 실험 행렬
2. 세 구현의 데이터 배치·접근 경로 그림 및 공통 update 흐름
3. N별 전체 update 시간 그래프: median과 IQR 등 산포 표시
4. 구현 간 시간 차이 또는 비율과 신뢰구간 표·그래프
5. Phase별 `ms/step`·비중 및 pair당 이웃 비용
6. 초기화 시간, 프로세스·컨테이너 메모리와 bytes per entity
7. 일반 View/CachedView 보조 결과, 캐시 결과표 또는 지원 제약표
8. 동등성 검증 범위와 반복 수·실패·이상치 처리 요약

모든 표·그림에 단위, 조건, 유효 반복 수와 원자료 run ID를 연결한다.
논문에는 구현 완료 후 수행한 측정 결과를 사용한다. 서론·배경·설계·방법은
구현과 함께 작성하고, 결과·논의·결론은 최종 분석 이후 수치를 확정한다.
학교의 제출 형식·분량·마감은 확인 후 이 문서의 협업 기록에 반영한다.

<a id="team"></a>

## 11. 팀 협업과 문서 갱신 규칙

연구 방향, 공통 설정, 측정 정의, 결과 채택 여부와 논문 계획은 이 문서에서
결정한다. `architecture.md`와 `tech-stack.md`는 상세 구현·기술 참고 자료이며,
실험 계획이나 완료 기준을 따로 복제하지 않는다. 제출된 계획서·신청서 PDF는
원래 승인된 주제의 근거로 보존한다.

작업을 시작할 때 로드맵 순서와 선행 조건을 확인한다. 담당자는 작업이 끝나면
기준 코드·검증·실험 근거와 영향을 받는 절을 갱신한다. 다른 팀원이 코드·조건과
기록을 대조한 뒤 완료 여부를 확인한다. 개인 작업 트리만으로 완료 체크하지 않는다.

역할과 일정은 팀 합의로 채운다. 담당자 이름, 마감이나 제출 규칙을 추정해
확정하지 않는다. 아래 양식을 이 문서 안에서 사용한다.

| 작업/이슈 | 담당자 | 검토자 | 목표일 | 상태·근거 commit/PR/run ID |
| --- | --- | --- | --- | --- |
| 다음 작업: #28 Archetype 통합 | 미정 | 미정 | 미정 | 미완료; 검증 후 기록 |
| 최종 실험 설정·데이터 수집 | 미정 | 미정 | 미정 | #29·#13 완료 후 확정 |
| 분석·논문·발표/시연 준비 | 미정 | 미정 | 미정 | 학교 제출 요건 확인 후 기록 |

### 결정·변경 기록

| 날짜 | 결정·변경 | 근거와 영향 |
| --- | --- | --- |
| 2026-10-13 | 두 연구 문서를 이 문서로 통합, #30 순서·수용 기준 반영 | 연구·실험·논문 기준 통합; 연구 범위 유지 |
| 2026-10-13 | 렌더링 추가 구현·시각화·렌더 성능 측정을 프로젝트와 논문 범위에서 제외 | Headless 시뮬레이션 비교에 집중; #11·#12 제외 및 #30 완료 기준 정리 |

이후 연구 범위·기본 경로·파라미터·horizon·통계 방법을 바꾸면 날짜, 이유,
근거와 재측정 필요 조건을 기록한다. 최종 코드 동결 후 변경하면 영향받는
조건을 재실행하고 이전 배치와 분리한다.

<a id="completion"></a>

## 12. 최종 완료·제출 체크리스트

다음 체크는 졸업 연구 전체의 완료 판정이다. 최종 동결 코드에서 검증한 근거를
확인한 뒤 각 항목의 완료 여부를 판정한다.

### 구현·정확성

- [ ] #30의 6개 선행 이슈 수용 기준을 충족한 코드가 하나의 기준 commit에 반영됨
- [ ] 최종 경로가 연속 객체 OOP / Sparse CachedView / 단일 고정 Archetype이며 동일 naive all-pairs 사용
- [ ] 후보 최적화·일반 View 보조 경로와 최종 기본 경로를 구분함
- [ ] VS2022 configure, Debug/Release 빌드·전체 테스트 및 세 CLI smoke 통과
- [ ] 최종 CMake의 clangd database 갱신
- [ ] 여러 seed·경계·퇴화 조건과 기본/확장 horizon에서 매 step 상태 동등성·허용 오차 검증
- [ ] OOP의 ECS 비의존성, RenderData와 App 책임 경계 유지

### 실험·재현성

- [ ] 코드·빌드·설정을 동결하고 commit/dirty/source/executable hash 기록
- [ ] 100/500/1000/3000의 같은 조건에서 조건별 최소 9회, 권장 12회 독립 반복
- [ ] 5000/10000 확장 지원과 실시 여부·제외 사유 기록
- [ ] 구현 순서 교차, 순차 실행, 동일 조건·horizon 유지
- [ ] 전체/phase 모드 분리, 초기화·캐시 생성·메모리 범위와 오버헤드 기록
- [ ] #22의 캐시 자료 또는 지원 제약 보고서 연결; 미측정 캐시 인과 주장 없음
- [ ] 실행별 원자료·환경 metadata·실패 로그 보존, checksum·조건·스키마 검증
- [ ] median/IQR/mean/SD/min/max, 차이 또는 비율의 신뢰구간과 유효 반복 수 산출
- [ ] 실패·이상치·재개·중복 처리 기준 기록
- [ ] 원자료만으로 표·그래프 재생성 및 대표 조건 재실행 안정성 확인
- [ ] 실행·측정·분석 명령과 원자료 위치를 최종 재현 기록에 기입

### 논문·제출물

- [ ] RQ1~RQ3에 최종 시간·phase·초기화·메모리 결과로 답변
- [ ] 일반 View/CachedView 보조 결과와 최종 세 구조 비교의 범위 명시
- [ ] 단일 CPU·단일 스레드·동종 workload·구조 변경 미평가 및 인과 해석 한계 명시
- [ ] 구현 완료 후 측정한 결과만 사용하고 모든 표·그림의 원자료·단위·반복 수 대조
- [ ] 참고문헌 서지·인용과 학교 형식·분량·제출 요건 확인
- [ ] README·architecture·tech-stack 및 이 문서의 실제 구현·미완료 상태 정합성 확인
- [ ] 실행 가능한 런타임·headless 3D Boids 시뮬레이션, 최종 소스 commit, 재현 자료와 논문 완성본 보존
- [ ] 팀원이 같은 환경에서 실행·재현 절차를 확인하고 제출본을 함께 검토

<a id="references"></a>

## 13. 참고문헌과 근거 자료

- Reynolds, C. W. (1987). *Flocks, Herds, and Schools: A Distributed Behavioral
  Model*. Computer Graphics, 21(4), 25–34.
  [저자 제공 원전](https://www.red3d.com/cwr/papers/1987/boids.html)
- Cox, L., Williams, B., Vickers, J., Ward, D., & Headleand, C. (2025).
  *Run-time Performance Comparison of Sparse-set and Archetype Entity-Component
  Systems*. Computer Graphics and Visual Computing (CGVC), The Eurographics Association.
  [출판사 자료](https://diglib.eg.org/items/6e291ae6-e32c-4c21-a89b-021fd9986ede),
  [DOI](https://doi.org/10.2312/cgvc.20251224)
