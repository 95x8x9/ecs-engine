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
| `App/` | 실행 옵션, 구현 선택, 콜백 연결과 결과 출력 |

`App`은 Engine과 Boids를 조립한다. Boids 행동은 `Boids`에 두고,
Engine에 도메인 로직을 넣거나 App에서 ECS 조회·Boids 계산을 수행하지 않는다.
`Boids::Common`은 Engine에 의존하지 않으며, OOP는 `Engine::ECS`에 의존하지 않는다.
두 시뮬레이션은 GLM만 사용하는 `Engine::RenderData`로 CPU Transform을 계산한다.

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

## 실행 흐름

Headless 벤치마크는 다음 순서로 실행한다.

1. App이 옵션을 읽고 ECS 또는 OOP 시뮬레이션을 생성·초기화한다.
2. Engine::Benchmark가 고정 dt로 Update 콜백을 호출하여 warmup을 수행한다.
3. 지정한 step 수만큼 같은 콜백을 호출하고 실행 시간을 측정한다.
4. App이 설정과 총·평균 갱신 시간, updates/s를 출력한다.

두 구현은 같은 seed와 설정으로 초기화하고 naive all-pairs 이웃 탐색을 사용한다.
한 step은 모든 Boid의 가속도 계산 → 속도·위치 갱신 → 경계 처리 → 방향 갱신 순서다.
ECS는 SystemManager에 등록된 System 순서로, OOP는 객체 메서드의 반복 호출로 수행한다.
이웃 계산에는 모든 Boid의 갱신 전 위치·속도를 사용한다.

계측은 Update 콜백 호출을 감싸며 초기화·warmup·출력은 제외한다.
Headless 경로는 그래픽 초기화를 수행하지 않는다. 기존 시각화는 별도로
Engine::Core의 Application 프레임 루프에서 프레임별 dt를 사용한다.
