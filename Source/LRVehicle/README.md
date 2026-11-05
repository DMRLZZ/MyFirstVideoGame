# LRVehicle

Coche del jugador de LEFTRIGHT: física de Chaos Vehicles, asistencias de conducción (Arcade y Simcade), conciencia de carretera en coordenadas `(S, D)`, detección de choques y anclajes de cámara. Todos los valores ajustables vienen de `Data/Vehicles/<id>.json`.

Depende de `LRCore` (tipos, bus de eventos, carga de datos), `LRWorld` (`ULRRoadSubsystem`) y del plugin `ChaosVehicles`.

## Clases principales

| Clase | Archivo | Para qué |
|---|---|---|
| `FLRVehicleDefinition` (+ subestructuras) | `Public/LRVehicleTypes.h` | Modelo de datos de un coche: motor, caja, diferencial, ejes (neumático, freno, suspensión), aerodinámica, dirección, cámaras, asistencias, umbrales de choque |
| `ALRVehicle` | `Public/LRVehicle.h` | Pawn del jugador (`AWheeledVehiclePawn` + `ILRRoadParticipant`). API de entrada, estado, carga de definición, `ResetForRun`, anclajes de cámara, eventos de choque y de cambio de carril |
| `ULRVehicleMovementComponent` | `Public/LRVehicleMovementComponent.h` | `UChaosWheeledVehicleMovementComponent` configurado en tiempo de ejecución desde la definición (`ApplyDefinition`) |
| `ULRWheelFront` / `ULRWheelRear` | `Public/LRVehicleWheel.h` | Clases de rueda de Chaos con valores por defecto de deportivo |
| `FLRLaneAssist` | `Public/Assist/LRLaneAssist.h` | Lógica pura: dirección del asistente de carril (Arcade) |
| `FLRStabilityAssist` | `Public/Assist/LRStabilityAssist.h` | Lógica pura: escala de dirección con la velocidad y contravolante tipo ESP (Simcade) |
| `LRVehicleMath` | `Public/LRVehicleMath.h` | Lógica pura: carriles con histéresis, paso de carril objetivo, limitador de velocidad, marcha inicial, velocidad de impacto, validación de definiciones |

La lógica pura (sin `UObject` ni mundo) vive en `Assist/` y `LRVehicleMath`, y es la que cubren los tests.

## Cómo funciona

- **Entrada:** el controlador del jugador, un piloto automático o una repetición llaman a `SetThrottleInput`, `SetBrakeInput`, `SetSteeringInput`, `SetHandbrakeInput` y `RequestLaneChange`. El coche nunca lee dispositivos.
- **Arcade:** `RequestLaneChange(-1/+1)` mueve el carril objetivo (solo carriles del sentido del jugador; en doble sentido también los contrarios, porque adelantar por ahí es el juego). Cada tick, `FLRLaneAssist` calcula la dirección a partir de `(S, D)`, el error de rumbo respecto a la carretera, la velocidad de guiñada y la curvatura. La física de Chaos sigue siendo real: los choques son de verdad.
- **Simcade:** dirección analógica escalada con la velocidad (`SteeringScaleCurve`) más contravolante cuando el coche gira más de lo pedido o derrapa.
- **Freno y marcha atrás:** el freno es analógico; mantenerlo por debajo de `ReverseEngageSpeedKmh` (5 km/h) engrana la marcha atrás, y el acelerador vuelve a primera.
- **Carretera:** cada tick se proyecta la posición con `ULRRoadSubsystem::WorldToRoad` usando una pista de segmento; el carril actual tiene histéresis y, cuando cambia, se emite `FLRLaneChangeEvent`.
- **Choques:** `OnComponentHit` de la malla → velocidad relativa proyectada en la normal → `FLRCrashEvent` (`bSevere` si supera `CrashImpactKmh`, 25 km/h por defecto). Golpes repetidos contra el mismo actor en menos de 0,25 s se ignoran; los contactos con normal casi vertical (suelo) también.
- **Accidente:** `SetWrecked(true)` suelta todas las entradas y deja que la física siga (el coche "planea").
- **Nueva partida:** `ResetForRun(S, D, VelocidadKmh)` teletransporta con `ETeleportType::ResetPhysics`, reconstruye el vehículo de Chaos, fija la velocidad lineal en la dirección de la carretera, anula la angular, elige marcha y sincroniza el giro de las ruedas.
- **Cámaras:** `GetCameraAnchorWorld(Modo)` devuelve el anclaje de cada modo (cabina, capó, parachoques, Viral, persecución cerca/lejos) a partir de `Cameras` en la definición.
- **Malla ausente:** si la malla esquelética no carga, se registra un error y se muestra una caja de reserva visible; el pawn no se destruye.

## Datos

| Archivo | Tipo | Contenido |
|---|---|---|
| `Data/Vehicles/bauer_r3_sport.json` | `VehicleDefinition` | Coche inicial, 6 cilindros turbo, propulsión, ~250 km/h, equilibrado |
| `Data/Vehicles/aurelion_vanta_s.json` | `VehicleDefinition` | Berlina V8 pesada, tracción total, muy estable, ~290 km/h |
| `Data/Vehicles/ironclad_hellion.json` | `VehicleDefinition` | Muscle car nervioso: mucho par, poco agarre atrás, ayudas débiles, sin control de tracción |
| `Data/Vehicles/ardent_vireo.json` | `VehicleDefinition` | Superdeportivo V10, tracción total, mucha carga aerodinámica, ~330 km/h |
| `Data/Schema/VehicleDefinition.schema.json` | esquema | Valida el objeto `data` (tipos y rangos) |

Las cuatro usan de momento los assets provisionales de la plantilla de vehículos de Unreal (`/Game/Vehicles/SportsCar/SKM_SportsCar`, `ABP_SportsCar`, huesos `Phys_Wheel_FL/FR/BL/BR`).

Las claves de `data` son exactamente los nombres de los `UPROPERTY`. Un campo que falte conserva el valor por defecto de C++. Valida con `Tools/Scripts/validate-data.ps1`.

## Cómo añadir un coche

1. Crea `Data/Vehicles/<id>.json` copiando el coche más parecido. El `id` del sobre, `data.Id` y el nombre del archivo deben coincidir (minúsculas y `_`). Marca ficticia (ADR-006).
2. Ajusta `SkeletalMesh` y `AnimClass` (ruta de clase terminada en `_C`) y los huesos de las ruedas (`FrontWheels.BoneNames`, `RearWheels.BoneNames`, izquierda primero). La malla necesita un Physics Asset.
3. Ajusta motor, caja y radio de rueda: la velocidad máxima por desarrollo (`MaxRpm`, última marcha, `FinalDriveRatio`, radio trasero) debe ser mayor que `TopSpeedKmh`, o la validación falla.
4. `LaneAssist.CurvatureFeedForward` ≈ batalla (m) / ángulo máximo de dirección (rad). `MaxLateralAccelMs2` define lo rápido que cambia de carril; no subas `HeadingGain` ni bajes `YawRateDamping` sin pasar los tests.
5. `StabilityAssist.SteeringScaleCurve` debe estar ordenada por velocidad y no crecer nunca.
6. Añade el `AudioProfile` correspondiente en `Data/Audio/` (módulo `LRAudio`).
7. Ejecuta `Tools/Scripts/validate-data.ps1` y `Tools/Scripts/test.ps1 -Filter LeftRight.Vehicle`: el test `LeftRight.Vehicle.Definitions` carga y valida todos los archivos de `Data/Vehicles/`.

Para cargarlo en juego: `ALRVehicle::LoadDefinitionById(TEXT("<id>"))`.

## Cómo extender

- Nuevo parámetro de manejo: añádelo a la subestructura de `LRVehicleTypes.h` con valor por defecto y comentario, aplícalo en `ULRVehicleMovementComponent::WriteSetupProperties` (o en `WriteAxleToWheel` si es de rueda), añádelo al esquema y a los JSON.
- Nuevo asistente: lógica pura en `Public/Assist/` con su spec, y solo la llamada en `ALRVehicle::ComputeSteering`.
- Valores de rueda: Chaos construye cada rueda desde el objeto por defecto de la clase de rueda, así que los valores por eje se escriben en los CDO de `ULRWheelFront/Rear` antes de reconstruir el vehículo. Solo hay un coche del jugador a la vez; si algún día hubiera varios coches con Chaos a la vez, habrá que crear clases de rueda por coche.

## Tests

| Spec | Qué comprueba |
|---|---|
| `LeftRight.Vehicle.LaneAssist` | Cambio de carril con un modelo de bicicleta cinemático a 100/200/300 km/h: sobreoscilación < 0,3 m, convergencia, tolerancia a retardo de neumático y curvas, mismo tiempo a cualquier velocidad |
| `LeftRight.Vehicle.StabilityAssist` | Escala de dirección monótona, guiñada deseada, contravolante |
| `LeftRight.Vehicle.Math` | Carriles e histéresis, carriles contrarios, limitador, rpm y marcha, velocidad de impacto |
| `LeftRight.Vehicle.Definitions` | Todos los JSON de `Data/Vehicles/` se cargan con `FLRDataLoader`, son válidos, plausibles y distintos entre sí |

## Presupuesto de rendimiento

- `ALRVehicle::Tick` (con `TRACE_CPUPROFILER_EVENT_SCOPE`): < 0,05 ms en el hilo de juego. Sin asignaciones de memoria por tick: solo aritmética, una proyección `WorldToRoad` con pista (O(1) amortizado) y llamadas de entrada a Chaos.
- La simulación de Chaos (un solo coche, 4 ruedas, raycast de suspensión) entra en el presupuesto de física de `docs/02-ARCHITECTURE.md §8`.
- `ApplyDefinition` reconstruye el vehículo físico: solo al cargar o cambiar de coche, nunca durante la partida.
