# 아키텍처

## 모듈 책임

이 문서는 headless 3D Boids 비교 연구의 설계 기준을 설명한다.

- `Engine`: 개체 생성·삭제, ECS 저장·조회, CPU 상태 데이터와 공통 계측 지원
- `Boids`: 공통 설정, OOP·Sparse Set·Archetype 시뮬레이션과 Dummy 변경
- `App`: 실행 설정, 시뮬레이션 연결, 실험 실행과 결과 출력·저장

의존 방향은 `App → Engine`, `App → Boids`, `Boids → Engine`이다.
Engine에 Boids 행동을 넣지 않고 App에 ECS 조회나 Boids 계산을 넣지 않는다.
OOP 구현은 `Engine/ECS`에 의존하지 않는다.

Engine·Boids·App은 기능별 폴더에 헤더와 소스를 함께 둔다. CMake 타깃은 프로젝트
루트를 include 경로로 사용하며 모듈 이름을 포함한 include를 유지한다.

## 데이터와 저장 방식

공통 설정과 `BoidParameters`는 Engine에 의존하지 않는 `Boids/Common`에 둔다.
Boid는 Transform, Velocity, Acceleration과 BoidParameters를 가지며, Dummy는
Transform과 Velocity만 가진다. Dummy는 모든 Boids 계산과 이웃 탐색에서 제외한다.

- OOP: 공통 부모 SimulationObject에서 BoidObject와 DummyObject를 상속하고,
  `std::vector<std::unique_ptr<SimulationObject>>`에 함께 저장한다.
  부모는 Transform·Velocity, BoidObject는 추가로 Acceleration·BoidParameters를 가진다.
- Sparse Set: 타입별 component 배열과 sparse/dense index를 사용한다.
- Archetype: Boid와 Dummy의 component 조합별 column 배열을 사용한다.

위치·회전·크기와 CPU 계산 함수는 GLM만 사용하는 `Engine::RenderData`를 사용한다.
렌더링 추가 구현과 성능 측정은 연구 범위에서 제외한다.

## 한 step의 처리

1. 개체 변경: 실행 비율에 따라 Dummy를 삭제하고 같은 수·구성으로 재생성
2. 조회 준비: 구조 변경을 확인하고 필요한 대상 목록·index 캐시 갱신
3. 이웃 계산: 모든 Boid의 갱신 전 상태로 가속도 계산
4. 상태 갱신: 속도 반영·제한, 위치 적분, 경계 처리와 방향 갱신

조회 준비에는 각 구현에서 실제로 필요한 작업만 포함한다. Sparse Set의 CachedView는
component 값 변경과 저장 구조 변경을 구분하며, 구조 변경 후 사용 전에 갱신한다.
순회 중에는 개체 생성·삭제나 component 추가·삭제를 수행하지 않는다.

각 구현은 같은 seed·생성 순서·논리적 Dummy 삭제 계획과 fixed timestep을 사용한다.
삭제로 배열 위치가 바뀌어도 Boid 데이터와 대응 관계를 유지한다.
`max_alignment_force`는 가중치 적용 전 Alignment 조향력만 제한한다.

## 계측 경계

전체 step 시간과 네 구간 시간은 같은 실행에서 측정한다. 전체 시간은 네 구간과
계측 비용을 포함한다. 초기화·warmup·결과 출력은 측정 step에서 제외한다.
시간 측정 종료 후 프로세스 최대 메모리와 컨테이너 저장량을 함께 기록한다.

Engine은 공통 timer와 측정 결과 구조를 제공하고, Boids는 네 처리 구간의 경계를
제공한다. App은 실행 파라미터와 결과 저장·분석을 연결한다. 계층 간 형식은
합의하되 구체적인 구현 작업·진행 상태는 GitHub 이슈에서 관리한다.

실험 조건·측정 정의·검증 기준은 [졸업 논문 통합 계획](research-goals.md)을 따른다.
