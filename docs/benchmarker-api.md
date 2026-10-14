# 벤치마크 API

실험 조건과 지표 정의는 [연구 기준](research-goals.md)의 5–6절을 따른다.
이 문서는 구현된 API와 다른 계층의 연결 방법을 설명한다.

## 모듈 연결

- `Engine::Measurement`: `Engine/Measurement/timing_recorder.h`와
  `Engine/Measurement/memory_measurement.h`. 시뮬레이션을 호출하지 않는 계측 도구.
- `Benchmarker::Experiment`: `Benchmarker/benchmarker.h`. 초기화·warmup·측정 반복과
  설정/환경/원자료/메모리 결과의 수집 및 직렬화.
- Boids: 실행 코드에서 계측 API를 호출하고, 소유한 저장소를 보고한다.
- App: 구현 선택과 실행 함수 연결, 출력 경로 및 파일 I/O.

일반 Engine 계측 사용자는 `Engine::Measurement`만 링크하면 된다. Boids 실행 함수를
사용하는 호출자는 `Benchmarker::Experiment`와 해당 Boids 타깃을 링크한다.
`Boids::Common`에는 Engine 의존성이 없다.

## 시간 측정

구간 이름을 먼저 등록한 뒤 `start/stop`을 사용한다. 등록된 이름은 도구가 소유한다.

```cpp
#include "Engine/Measurement/timing_recorder.h"

engine::measurement::TimingRecorder timing({"phase1", "phase2"});
timing.begin_measurement(10);
for (int step = 0; step < 10; ++step) {
    timing.begin_step_measurement();
    timing.start("phase1");
    // phase1 작업
    timing.stop("phase1");
    timing.record_zero("phase2"); // 실제로 작업이 없는 구간
    timing.end_step_measurement();
}
const auto result = timing.result();
```

이름 조회를 피하려면 `register_section("phase1")`의 반환값 `SectionId`를
`start(id)/stop(id)`에 전달한다. ID는 해당 TimingRecorder 인스턴스에 속한다.
측정 중 구간을 등록하거나 버퍼를 확장하지 않는다. 문자열/ID 호출 모두 정상 경로에
동적 할당이 없으며, 구간별 계측 비용은 전체 step 시간에 포함된다.

구간 중첩은 서로 다른 이름에 허용한다. 같은 구간을 한 step에서 여러 번 호출하면
시간과 호출 횟수를 합산한다. 구간 평균의 분모는 호출 횟수가 아니라 전체 measured
steps다. 중첩된 구간의 합은 전체 시간보다 클 수 있으므로 전체 시간은 따로 측정한다.

`record_zero`는 작업이 없다는 명시적 기록이며 시간은 0, start/stop 호출 횟수는 0이다.
기록하지 않은 구간은 `SectionSample::recorded == false`다. 하나라도 누락된 구간의
`mean_ms_per_step`은 `std::nullopt`이며 `total_ms`는 기록된 구간의 부분합이다.

warmup은 `set_enabled(false)` 상태에서 동일 실행 API를 호출한다. 이때 계측 호출은
시계 조회·이름 조회도 생략한다. `begin_measurement(steps)`는 기록 공간을 할당하고
기존 구간 시간을 초기화하며 계측을 활성화한다. 하나의 객체는 한 측정 실행에 사용한다.

잘못된 시작/종료, step 안에서 미종료 구간, 측정 중 모드 변경 등은 `std::logic_error`를
던지고 실행을 무효화한다. 예외 발생 실행을 정상 결과로 내보낼 수 없다. 빈 구간 이름과
0 measured steps는 `std::invalid_argument`다. 단일 스레드 전용이며 전역 상태는 없다.

초기화처럼 한 번만 측정하려면 `begin_measurement` 없이 등록된 구간의
`start/stop`을 호출하고 `total_ms(name)`으로 읽는다. 이 방식은 step별 원자료를 생성하지 않는다.

## 메모리 보고

```cpp
#include "Engine/Measurement/memory_measurement.h"

engine::measurement::StorageReport report;
report.add_vector("object_pointers", objects); // vector<unique_ptr<Base>>
report.add_objects<BoidObject>("boid_objects", boid_count);
report.add_objects<DummyObject>("dummy_objects", dummy_count);
```

`add_vector`는 원소의 size/capacity와 `sizeof(T)`를 사용한다. 포인터 컨테이너는
포인터 저장량만 계산하므로 실제 할당 객체는 `add_objects`로 별도 보고한다. 상속 객체는
실제 파생 타입 크기를 사용한다. 객체 내부 필드는 다시 더하지 않는다.

특수 저장소는 `add_bytes(name, size_bytes, capacity_bytes)`를 사용한다.
`vector<bool>`은 이 API로 실제 저장량을 별도 보고해야 한다. 이름 중복과 size > capacity는
오류이며, 곱셈/합산의 `size_t` overflow도 검출한다.

미지원/미측정 항목은 `add_unavailable(name, status, reason)`으로 등록한다.
`complete()`가 false라면 `size_bytes()/capacity_bytes()`는 알려진 항목의 부분합이다.
소유자가 동일한 할당을 서로 다른 이름으로 중복 보고하지 않도록 해야 한다.

현재 OOP는 `vector<BoidObject>`를 보고한다. ECS는 Registry의 Entity 보조 배열과
component sparse/dense 배열, CachedView, 시스템 포인터 배열과 할당 객체를 보고한다.
unordered_map의 payload 이외 node/bucket 저장량은 미지원이므로 ECS 합계는 부분합이다.
시뮬레이션 객체 자체를 포함하며, 그 안의 컨테이너 제어 필드는 별도로 더하지 않는다.

`QueryProcessMemory()`는 마지막 시간 측정 후, 보고서 생성·시뮬레이션 파괴 전에
호출한다. Windows에서는 Peak Private Commit과 Peak Working Set을 byte 단위로
반환한다. 다른 OS는 `Unsupported`, OS 조회 실패는 `Failed`와 사유를 반환한다.
실제 0과 누락은 `std::optional`로 구분한다.

## 실험 실행 및 결과

```cpp
const auto result = benchmarker::RunBenchmark<boids::ecs::BoidSimulation>(
    simulation_config, benchmark_config, "ecs");
```

새 구현은 `Initialize()`, `Update(float, TimingRecorder&)`, `GetStorageReport() const`를
제공하면 된다. 공통 부모나 콜백은 필요하지 않다. Update에서 네 구간 이름
`entity_changes`, `query_preparation`, `neighbor_calculation`, `state_update`를 사용한다.
실제 작업이 없는 구간은 `record_zero`로 보고한다. 기존 무계측 Update 오버로드도 유지한다.

현재 실행 함수는 Boid 전용 ECS/OOP를 지원하며 Dummy 변경·상속 기반 OOP·Archetype
연구 구현은 포함하지 않는다. CSV에도 이 실행 범위를 명시한다.

```powershell
cmake --build --preset vs2022-release
.\build\vs2022\App\Release\App.exe --benchmark --implementation ecs --agents 100 --warmup 10 --steps 100 --output out/ecs.csv
```

`--output` 생략 시 `out/benchmarks/<implementation>-<run-id>.csv`를 생성한다.
명시한 출력 경로는 파일을 덮어쓰므로 반복 실행에서는 경로를 달리하거나 기본 경로를 사용한다.
측정 프로세스는 동시에 실행하지 않는다.

CSV 열은 `kind,name,step,value,status,reason`이다. step은 0부터 시작한다.
설정, 환경, 초기화/시간 요약, step별 전체 시간과 구간 시간/호출 수, 프로세스 최대
메모리, 저장소별 size/capacity, bytes per entity와 pair당 비용을 보존한다.
누락 값은 빈 value와 상태/사유로, 실제 0은 숫자 0으로 저장한다. 부분합은 `partial`이다.
수치 문자열은 locale에 독립적이며 부동소수점 원자료를 보존할 정밀도를 사용한다.

원자료와 설정/메타데이터는 같은 실행의 결과다. CPU·OS 정보는 현재 Windows에서
수집하고 컴파일러·CMake 구성·빌드 플래그·C++ 표준도 기록한다. OS는 Windows와
빌드 번호·릴리스 값으로 식별하고 Registry의 제품명 원문도 별도 필드에 보존한다. 출력과 외부 캐시
프로파일링은 시간 계측에 포함하지 않는다.
