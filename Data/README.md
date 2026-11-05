# Data: datos de diseño de LEFTRIGHT

Esta carpeta es la **fuente de verdad** de todos los valores ajustables del juego (ADR-002): reglas de puntuación, tráfico, vehículos, cámaras, entrada, audio, economía, etc. El código C++ define las reglas y los valores por defecto; el JSON de aquí los sobrescribe en tiempo de ejecución.

`LeftRight.Build.cs` copia `Data/` a la build empaquetada, y el juego la lee con `FLRDataLoader` / `ULRDataSubsystem` (módulo `LRCore`).

## Estructura

```
Data/
├─ Schema/                    ← un JSON Schema (draft 2020-12) por tipo
│  ├─ Envelope.schema.json    ← sobre común a todos los archivos
│  └─ <Tipo>.schema.json      ← valida el objeto "data" de cada tipo
├─ Gameplay/  Traffic/  Vehicles/  Input/  Camera/  Audio/  Economy/  ...
│  └─ <Nombre>.json
└─ README.md
```

## Formato del sobre (envelope)

Todos los archivos tienen exactamente esta forma:

```json
{
	"type": "ScoringRules",
	"id": "default",
	"version": 1,
	"data": {
		"NearMissBasePoints": 100,
		"bComboEnabled": true,
		"Tier": "VeryClose",
		"Offset": { "X": 0.0, "Y": 0.0, "Z": 120.0 }
	}
}
```

| Campo | Obligatorio | Descripción |
|---|---|---|
| `type` | sí | Nombre del tipo en PascalCase. Debe existir `Data/Schema/<type>.schema.json`. |
| `id` | sí | Identificador único dentro de su tipo (`default`, `sport_coupe`...). Letras, números, `_`, `.` y `-`. |
| `version` | sí | Versión del esquema de este archivo (entero ≥ 1). Se incrementa cuando el struct cambia de forma incompatible. |
| `data` | sí | Objeto con los valores. Sus claves son **exactamente** los nombres de las `UPROPERTY` del struct C++ destino. |
| `comment` | no | Texto libre para diseñadores; el juego lo ignora. |
| `$schema` | no | Pista para el editor de texto; el juego lo ignora. |

El esquema del sobre está en [`Schema/Envelope.schema.json`](Schema/Envelope.schema.json) y no admite otras claves de primer nivel.

## Reglas de nombres y valores

- **Archivos:** `Data/<Área>/<Nombre>.json`, con el área en PascalCase (`Gameplay/Scoring.json`, `Vehicles/sport_coupe.json`). Para vehículos y perfiles con muchos ejemplares, el nombre del archivo coincide con el `id`.
- **Tipos:** PascalCase, sin el prefijo `FLR` del struct (`FLRScoringRules` → `ScoringRules`). Un esquema por tipo: `Data/Schema/<Tipo>.schema.json`.
- **Claves dentro de `data`:** idénticas a la `UPROPERTY` (PascalCase; los booleanos llevan el prefijo `b`: `bTwoWay`).
- **Enums:** como cadena con el nombre del valor (`"Tier": "VeryClose"`, `"Mode": "EndlessTwoWay"`).
- **Vectores y colores:** `FVector` → `{"X":..,"Y":..,"Z":..}`, `FVector2D` → `{"X":..,"Y":..}`, `FLinearColor` → `{"R":..,"G":..,"B":..,"A":..}`.
- **Unidades:** SI (metros, segundos, m/s, kg) salvo que el nombre del campo diga otra cosa (`SpeedLimitKmh`, `LengthMeters`). Nada de centímetros en los datos.
- **Rutas de assets:** rutas de objeto (`/Game/LeftRight/Vehicles/...`).
- **Campos ausentes** conservan el valor por defecto de C++, y los **campos desconocidos** se ignoran en el juego (pero el validador los rechaza si el esquema tiene `additionalProperties: false`, que es lo habitual).
- Las carpetas que empiezan por `_` o `.` se consideran temporales (por ejemplo, `_AutomationTmp_*` creadas por los tests) y el validador las ignora. No pongas datos reales en ellas.
- La pareja (`type`, `id`) debe ser única en todo `Data/`.

## Cómo añadir un tipo nuevo

1. Define el `USTRUCT` en C++ con valores por defecto sensatos en todos los campos.
2. Crea `Data/Schema/<Tipo>.schema.json` (draft 2020-12, `additionalProperties: false`, tipos y rangos razonables). Puedes compartir definiciones con `$ref` relativo entre esquemas (por ejemplo, `"Common.schema.json#/$defs/Vector"`).
3. Crea el archivo de datos `Data/<Área>/<Nombre>.json` con el sobre.
4. Cárgalo en C++: `FLRDataLoader::Load(TEXT("<Área>/<Nombre>.json"), MiStruct)` o `ULRDataSubsystem::Get(this)->Get(...)`.
5. Valida (siguiente sección).

## Cómo validar

Sin abrir el motor (Python 3 + `jsonschema`):

```powershell
python -m pip install -r Tools/Data/requirements.txt   # una sola vez
Tools/Scripts/validate-data.ps1                         # o: python -I Tools/Data/validate_data.py
Tools/Scripts/validate-data.ps1 -Strict                 # los avisos (tipo sin esquema) cuentan como error
```

El validador comprueba que cada archivo sea JSON UTF-8 válido, que el sobre cumpla `Envelope.schema.json`, que `data` cumpla `<type>.schema.json` (avisa si falta el esquema), que los propios esquemas sean válidos y que no haya parejas (`type`, `id`) duplicadas. Devuelve código 1 si hay errores. Es lo primero que ejecuta la CI.

## Recarga en caliente

Con el juego o el PIE en marcha, edita el JSON y ejecuta en la consola:

```
lr.Data.Reload
```

`ULRDataSubsystem::ReloadAll()` emite `OnDataReloaded` y cada sistema suscrito vuelve a leer sus archivos. Si un archivo no se puede leer o convertir, el sistema conserva los valores anteriores y el error aparece en el log (`LogLRCore`).
