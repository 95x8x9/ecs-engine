# Tech Stack

본 프로젝트는 C++20 기반의 경량 ECS 런타임에서 OOP, Sparse Set ECS와 Archetype ECS의
3D Boids 시뮬레이션 성능을 headless로 비교한다. 빌드 구성은 CMake, 외부 패키지는
vcpkg 매니페스트로 관리한다. 기존 그래픽 기능의 OpenGL 3.3 Core Profile, GLFW,
GLAD와 Dear ImGui는 현재 빌드 의존성으로 유지한다.

3D 범위는 위치·속도와 Boids 행동을 3차원 공간에서 계산하는 것이다. 렌더링 추가
구현과 시각화·FPS·프레임·렌더 CPU/GPU 성능 측정은 프로젝트와 논문 범위에서 제외한다.

## Engine Scope

완료 대상 런타임은 시뮬레이션 수명·update loop·fixed timestep, ECS 객체 관리,
headless 실행과 benchmark 측정에 필요한 기능이다. 기존 그래픽 모듈은 추가 개발
목표나 완료 검증 대상이 아니다.

## Components

| Technology | Role |
| --- | --- |
| C++20 | 런타임, 세 시뮬레이션과 ECS 구조 구현 |
| GLM | 시뮬레이션 벡터·회전 및 CPU Transform 계산 |
| OpenGL 3.3 Core Profile | 기존 시각화의 렌더링 백엔드; 연구 완료 범위 밖 |
| GLFW | 기존 시각화의 창·컨텍스트·입력; headless에서는 초기화하지 않음 |
| GLAD | 기존 그래픽 기능의 OpenGL 함수 로딩 |
| Dear ImGui | 기존 시각화 UI; benchmark 측정 범위 밖 |
| CMake | 빌드 구성 및 타깃 관리 |
| vcpkg | GLFW 등 외부 라이브러리 설치 및 버전 관리 |

## Dependency Policy

GLFW와 Dear ImGui는 vcpkg 매니페스트(`vcpkg.json`)로 관리한다. Dear ImGui는 공식 GLFW 및 OpenGL 3 백엔드만 활성화한다. GLAD는 OpenGL API 버전과 프로파일에 따라 생성되는 코드이므로 `third_party/glad`에 프로젝트 의존성으로 포함한다.

## Headless Runtime Scope

- OOP / Sparse Set / Archetype 구현 선택과 공통 초기 조건
- Fixed timestep 기반 simulation update
- 전체·phase별 update 및 초기화 시간·메모리 측정
- 독립 프로세스 반복 실행, 원자료·환경 metadata 보존과 통계 분석

`Engine::Benchmark`는 `std::chrono::steady_clock`과 업데이트 콜백을 사용하는
표준 C++ 전용 타깃이다. 벤치마크 모드에서는 GLFW/OpenGL/ImGui를 초기화하지
않는다. 실행 파일은 시각화 모드와 공유하므로 기존 링크 의존성은 유지한다.

런타임 코드는 `Engine`, 연구 도메인은 `Boids`, 실행 조립은 `App` CMake 타깃으로
분리한다. ECS 렌더 어댑터는 별도 `Engine::ECSRenderer` 타깃이며 공통
`Engine::Renderer`는 ECS에 의존하지 않는다.

Transform과 MeshRenderer 등 공통 렌더 데이터는 `Engine/Renderer/`와
`engine::renderer` namespace에 둔다. GLM만 사용하는 `Engine::RenderData`
타깃을 별도로 링크하므로 Boids는 OpenGL 없이 Transform 함수를 사용할 수 있다.

`Boids/Common`은 Engine 의존성이 없는 헤더 전용 `Boids::Common` 타깃으로
공통 설정과 행동 매개변수를 제공한다. `Boids/ECS`와 `Boids/OOP`가 이를 사용하며,
OOP는 연속 객체 배열로 같은 규칙을 구현한다.

Engine·Boids·App은 기능별 폴더에 헤더와 소스를 함께 배치한다. 각 타깃의
헤더 검색 경로는 `${PROJECT_SOURCE_DIR}`이며, 기존 모듈 이름을 포함한
include 경로와 CMake 타깃 간 의존 관계를 유지한다.

## Existing Graphics Modules

현재 ECS 시각화·카메라·instancing·UI와 그래픽 의존성은 기존 구현으로 유지한다.
공통 OOP/Archetype 시각화, 렌더 어댑터 추가, FPS·frame·render CPU/GPU 계측은
후속 구현 목표에서 제외한다. Headless 실험에서 그래픽 초기화·호출을 수행하지 않는다.
완료 범위와 실험 절차는 [졸업 논문 통합 계획](research-goals.md)을 따른다.
