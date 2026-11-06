# LRCamera

Cámaras del jugador de LEFTRIGHT: persecución lejana y cercana, capó, cabina, parachoques y Cámara Viral (GoPro, dashcam y móvil). Todos los valores ajustables vienen de `Data/Camera/Cameras.json`.

Depende de `LRCore` (tipos, bus de eventos, carga de datos), `LRVehicle` (`ALRVehicle`, anclajes de cámara) y `LRWorld` (superficie de la carretera, carriles y curvatura).

## Clases principales

| Clase | Archivo | Para qué |
|---|---|---|
| `ALRPlayerCameraManager` | `Public/LRPlayerCameraManager.h` | Calcula la vista cuando el objetivo es un `ALRVehicle`; con cualquier otro objetivo usa el comportamiento de `APlayerCameraManager`. Modos, mirar atrás, presets y ajustes de usuario |
| `FLRCameraConfig` (+ subestructuras) | `Public/LRCameraTypes.h` | Modelo de datos: persecución, montadas, cabina, vibración, mirada anticipada y Cámara Viral |
| `TLRCriticallyDampedSpring`, `FLRCriticallyDampedSpring`, `FLRCriticallyDampedSpringDouble`, `FLRCriticallyDampedSpringVector` | `Public/LRCameraSpring.h` | Lógica pura: muelle críticamente amortiguado con solución exacta, independiente de los fotogramas (float con ángulos, double y vector) |
| `FLRCockpitHeadModel` | `Public/LRCockpitHeadModel.h` | Lógica pura: muelle-amortiguador de la cabeza sobre las fuerzas G, con subpasos fijos y límites |
| `FLRCameraMath` | `Public/LRCameraMath.h` | Lógica pura: curvas de FOV, vibración determinista con ruido Perlin, mirada anticipada, estabilización del horizonte, ciclo de modos y overrides de post-proceso |

La lógica pura (sin `UObject` ni mundo) es la que cubren los tests. `ALRPlayerCameraManager` solo la conecta con el coche y la carretera.

## Cómo funciona

| Modo | Comportamiento |
|---|---|
| **ChaseFar / ChaseNear** | Brazo detrás del coche con retardo de guiñada críticamente amortiguado. Distancia y altura de `FLRCameraAnchors` (definición del coche), que se estiran con la velocidad y la aceleración. Mira a un punto por delante del coche, añade la mirada anticipada hacia el carril destino y la curva, y ensancha el FOV con la velocidad. Una guarda mantiene la cámara por encima del asfalto. Horizonte siempre nivelado |
| **Hood / Bumper** | Rígidas sobre la carrocería, con vibración de alta frecuencia |
| **Cockpit** | Ojos del piloto + `FLRCockpitHeadModel`. Quita parte del cabeceo y el balanceo de la carrocería (`HorizonStabilization`). FOV base entre 60 y 95 (`SetCockpitFov`) |
| **Viral** | Preset con anclaje, desplazamiento, FOV, vibración y post-proceso propios |

- **Ciclo:** `ChaseFar → ChaseNear → Hood → Cockpit → Bumper → Viral → ChaseFar` (`CycleCameraMode`).
- **Mirar atrás:** `SetLookBack(true)` gira la vista 180°. En persecución, el brazo pasa por delante del coche. Las cámaras montadas giran sobre su sitio, así que en capó y parachoques se ve la propia carrocería.
- **Evento:** `FLRCameraModeChangedEvent` se emite en cada cambio de modo, cuando un preset o una recarga cambia `bInterior`, y una vez al empezar, en la primera actualización real (cuando todos los `BeginPlay` ya han enlazado sus oyentes). `bInterior` es verdadero en cabina y en los presets interiores (Dashcam y Phone).
- **Reinicios:** `OnRunReset`, un cambio de modo, un cambio de coche o un salto mayor que `TeleportSnapDistanceCm` en una actualización colocan todos los muelles directamente en su objetivo.
- **Vibración:** es función del tiempo, sin aleatoriedad. El reloj vuelve a cero con cada `OnRunReset`, de modo que una misma partida da la misma vibración. La escala depende de la velocidad y del firme (`RoadRoughness` en carril, `ShoulderRoughness` fuera).
- **Post-proceso Viral:** se escribe en `FMinimalViewInfo::PostProcessSettings` con sus `bOverride_*`. Como la vista parte de cero en cada fotograma, los efectos desaparecen solos al salir del modo.

## Comandos de consola

| Comando | Efecto |
|---|---|
| `lr.Camera.ViralPreset <GoPro\|Dashcam\|Phone>` | Cambia el preset de la Cámara Viral |
| `lr.Camera.ViralArtifacts <0\|1>` | Desactiva o activa los artefactos de post-proceso |
| `lr.Camera.Cycle` | Pasa al siguiente modo |
| `lr.Data.Reload` | Vuelve a leer `Cameras.json`, conservando los ajustes de usuario |

## Accesibilidad

- `bViralArtifactsEnabled` (`SetViralArtifactsEnabled`): quita todos los overrides de post-proceso de la Cámara Viral (grano, aberración, viñeta, desenfoque, bloom, exposición lenta, nitidez y color). El FOV, el montaje y la vibración del preset se mantienen.
- `GlobalShakeScale` (`SetShakeScale`): multiplica toda la vibración procedural; 0 la elimina, incluida la mano del preset Phone.
- `HeadModel.Intensity` (`SetHeadMotionIntensity`): 0 deja la cabeza rígida en cabina.

## Datos

| Archivo | Tipo | Contenido |
|---|---|---|
| `Data/Camera/Cameras.json` | `CameraConfig` | Toda la configuración. Replica los valores por defecto de C++ |
| `Data/Schema/CameraConfig.schema.json` | esquema | Valida el objeto `data` (tipos y rangos) |

Las claves de `data` son exactamente los nombres de las `UPROPERTY`. Un campo que falte conserva el valor por defecto de C++. Las distancias van en centímetros y lo indica el nombre del campo (`...Cm`), porque se aplican directamente en el espacio del mundo. En `PostProcess`, un valor negativo deja el ajuste del motor o del volumen sin tocar.

## Cómo extender

- **Nuevo preset Viral:** añade el valor a `ELRViralPreset`, su campo en `FLRViralCameraParams`, los casos de `GetPreset`, `GetViralPresetName` y `ParseViralPreset`, los valores por defecto en el constructor de `FLRCameraConfig`, y el preset en el JSON y el esquema.
- **Nuevo override de post-proceso:** añade el campo a `FLRViralPostProcessParams` (valor negativo = sin tocar), escríbelo en `FLRCameraMath::ApplyViralPostProcess` y añádelo al esquema. Comprueba el nombre del `bOverride_` en `Engine/Scene.h`.
- **Nuevo modo:** el enum `ELRCameraMode` está en `LRCore`. Añade el modo allí, en `GetNextCameraMode` e `IsInteriorMode`, y una función `Update*View` en el manager. La matemática nueva va en `FLRCameraMath`, con su spec.
- **Lugar de la cámara en un coche:** se ajusta en `Cameras` de `Data/Vehicles/<id>.json`, no aquí.

## Tests

| Spec | Qué comprueba |
|---|---|
| `LeftRight.Camera.Spring` | Convergencia, sin sobreoscilación, independencia de los fotogramas, pasos enormes, ángulos por el arco corto, vector y double |
| `LeftRight.Camera.HeadModel` | Escalón de G: converge y las oscilaciones nunca crecen, límites con 50 G, tirones, intensidad, sentido de la inercia, reinicio |
| `LeftRight.Camera.Math` | FOV monótono, vibración determinista y acotada, escala con velocidad y firme, mirada anticipada, horizonte, ciclo de modos, nombres de presets, overrides de post-proceso |
| `LeftRight.Camera.Config` | `Cameras.json` carga, es coherente y coincide con los valores por defecto; `Sanitize` repara valores rotos |

```powershell
Tools/Scripts/test.ps1 -Filter LeftRight.Camera
Tools/Scripts/validate-data.ps1
```

## Presupuesto de rendimiento

- `ALRPlayerCameraManager::UpdateViewTarget` (con `TRACE_CPUPROFILER_EVENT_SCOPE`): objetivo < 0,05 ms en el hilo de juego. Pendiente de medir en juego.
- Sin asignaciones por fotograma en nuestro código: solo aritmética, una proyección `WorldToRoad` con pista (O(1) amortizado, solo en persecución) y una búsqueda de curvatura. La copia de `FMinimalViewInfo` al inicio es la misma que hace el motor.
- La Cámara Viral solo escribe ajustes de post-proceso; su coste está en el renderizado y depende del preset.
