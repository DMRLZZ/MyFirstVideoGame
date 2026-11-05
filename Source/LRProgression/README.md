# LRProgression

Guardado del perfil del jugador y economía: créditos, garaje (coches comprados y seleccionado), mejoras por coche, récords por modo y estadísticas acumuladas (ver `docs/02-ARCHITECTURE.md §5.8` y `docs/04-GAMEPLAY.md §7`).

Depende solo de `LRCore`. No participa en la simulación, así que no afecta al determinismo.

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `ULRProgressionSubsystem` | `Public/LRProgressionSubsystem.h` | `UGameInstanceSubsystem` dueño del perfil. Carga la economía, carga/guarda el slot `LeftRight_Profile`, compra coches y mejoras, selecciona coche y registra el resultado de cada partida. Emite `OnProfileChanged`. |
| `ULRSaveGame` | `Public/LRSaveGame.h` | `USaveGame` versionado (`SaveVersion`): créditos, `OwnedVehicles`, `SelectedVehicle`, `Upgrades`, `BestScores` (clave = nombre de `ELRGameModeType`), distancia, partidas y near misses totales. |
| `LRSaveMigration` | `Public/LRSaveMigration.h` | Migraciones puras paso a paso (`Migrate`, `MigrateWithSteps`). Versión actual: 1. El paso v0 → v1 normaliza perfiles del prototipo sin versión. |
| `FLREconomyCalculator` | `Public/LREconomyCalculator.h` | Lógica pura: créditos de una partida, precios de coches y mejoras, validación de compras y actualización de récords. |
| `FLREconomyRules`, `FLRVehiclePrice`, `FLRModeCreditMultiplier`, `FLRVehicleUpgradeState`, `ELRUpgradeType`, `ELRPurchaseResult` | `Public/LRProgressionTypes.h` | Tipos de datos y resultado de las compras. |

### Fórmula de créditos

```
créditos = floor((Puntuación × CreditsPerPoint + Km × DistanceCreditsPerKm) × multiplicador_del_modo)
créditos = max(créditos, MinCreditsPerRun)   // solo si la partida puntuó o recorrió algo
```

Precio de una mejora del nivel `n` al `n+1`: `UpgradeLevelPrices[n] × UpgradeCostMultiplier` del coche. Hay 7 categorías (motor, turbo, transmisión, neumáticos, frenos, suspensión, aerodinámica) con niveles 0 a 5.

### Uso desde otros módulos

```cpp
ULRProgressionSubsystem* Progression = ULRProgressionSubsystem::Get(this);
const FName CarId = Progression->GetSelectedVehicle();          // coche para ALRVehicle::LoadDefinitionById
const int64 Earned = Progression->RecordRunResult(Result, Mode); // al terminar la partida (guarda solo)
if (Progression->PurchaseVehicle(TEXT("volta_nine"))) { ... }   // garaje / UI
```

## Archivos de datos

| Archivo | Tipo | Esquema |
|---|---|---|
| `Data/Economy/Economy.json` | `EconomyRules` | `Data/Schema/EconomyRules.schema.json` |

Contiene los precios de los 10 coches de la v1 (de `bauer_r3_sport`, gratis, a `ardent_spectre_lm`, 1.000.000), los precios base de las mejoras y los multiplicadores por modo. Se recarga en caliente con `lr.Data.Reload`.

## Cómo extender

- **Nuevo coche a la venta:** añade una entrada en `VehiclePrices` con el mismo id que `Data/Vehicles/<id>.json`.
- **Cambiar el formato del guardado:**
  1. Añade o cambia los `UPROPERTY(SaveGame)` de `ULRSaveGame`.
  2. Sube `LRSaveMigration::CurrentVersion`.
  3. Escribe la función del paso `N-1 → N` y regístrala en `GBuiltInSteps` (`Private/LRSaveMigration.cpp`). Un `static_assert` obliga a tener un paso por versión.
  4. Añade un caso en `LRSaveMigrationSpec.cpp` con un perfil falso de la versión anterior.
- **Nueva categoría de mejora:** añade el valor a `ELRUpgradeType` (antes de `Count`), el campo en `FLRVehicleUpgradeState` y el `case` en `LRProgressionTypes.cpp`.
- Un perfil escrito por una versión más nueva del juego, o ilegible, **nunca se sobrescribe**: se juega con un perfil temporal en memoria.

## Tests

- `LeftRight.Progression.Migration`: migración v0 → v1, perfiles actuales y futuros, ejecutor paso a paso con pasos falsos.
- `LeftRight.Progression.Economy`: cálculo de créditos, precios, compras (no se compra dos veces, créditos insuficientes, coche desconocido), récords por modo y lectura del JSON.

Todos son de lógica pura (sin mundo) y no tocan el disco.

## Presupuesto de rendimiento

- Sin tick. Coste cero por fotograma.
- Guardado síncrono (perfil de pocos KB, < 1 ms) solo en acciones del jugador: compra, selección, mejora y fin de partida. Nunca durante la conducción.
- La carga del perfil y de la economía ocurre una vez al iniciar la instancia de juego.
