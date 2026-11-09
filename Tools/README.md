# Tools: scripts de compilación, tests y datos

Envoltorios de línea de comandos para compilar, probar, validar datos, empaquetar y lanzar LEFTRIGHT sin abrir el editor. Funcionan en **Windows PowerShell 5.1** y en PowerShell 7.

## Requisitos

- Unreal Engine 5.8 instalado. Los scripts usan `$env:UE_ROOT` y, si no existe, `C:\Program Files\Epic Games\UE_5.8`.
- Visual Studio 2022 con las cargas de trabajo de C++ para juegos (para `build.ps1` y `package.ps1`).
- Python 3.10 o superior con los paquetes de `Tools/Data/requirements.txt` (solo para `validate-data.ps1`).
- Si la política de ejecución bloquea los scripts: `powershell -ExecutionPolicy Bypass -File Tools/Scripts/<script>.ps1`.

```powershell
$env:UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'   # opcional si usas la ruta por defecto
```

Todos los scripts se pueden ejecutar desde cualquier carpeta: calculan la raíz del repositorio a partir de su propia ubicación. Los logs van a `Saved/Logs/`.

## Estructura

```
Tools/
├─ Scripts/
│  ├─ common.ps1          ← funciones compartidas (no se ejecuta solo)
│  ├─ build.ps1
│  ├─ test.ps1
│  ├─ validate-data.ps1
│  ├─ run-python.ps1
│  ├─ package.ps1
│  └─ play.ps1
├─ Data/
│  ├─ validate_data.py    ← validador de Data/**/*.json
│  └─ requirements.txt
└─ README.md
```

## Scripts

### `common.ps1`

Biblioteca que los demás scripts cargan con `. "$PSScriptRoot\common.ps1"`. Ofrece:

| Función | Qué hace |
|---|---|
| `Get-LRRepoRoot` | Ruta absoluta de la raíz del repositorio. |
| `Get-LRUERoot` | Raíz del motor (`$env:UE_ROOT` o la ruta por defecto). Falla con un mensaje claro si no contiene `Engine/`. |
| `Get-LRProjectFile` | Ruta de `LeftRight.uproject`. Falla si no existe. |
| `Get-LREngineFile <ruta>` | Ruta de un archivo dentro de `<UE_ROOT>/Engine`. Falla si no existe. |
| `Get-LRLogDirectory` | `Saved/Logs`, creada si hace falta. |
| `Invoke-LRProcess` | Ejecuta un programa o `.bat` con una línea de argumentos literal, muestra la salida en directo, la copia a un log y devuelve el código de salida. Evita los problemas de comillas de PowerShell 5.1 y de rutas con espacios. |
| `ConvertFrom-LRJsonFile` | Lee JSON grande (con o sin BOM) en PowerShell 5.1 y 7. |
| `Stop-LRScript` | Muestra un error y termina con código 1. |

### `build.ps1`

Compila con UnrealBuildTool (`Engine/Build/BatchFiles/Build.bat <Target> Win64 <Config> -Project=<uproject> -WaitMutex -NoHotReload`).

```powershell
Tools/Scripts/build.ps1                                          # LeftRightEditor Win64 Development
Tools/Scripts/build.ps1 -Configuration DebugGame
Tools/Scripts/build.ps1 -Target LeftRight -Configuration Shipping
```

- `-Target`: `LeftRightEditor` (por defecto) o `LeftRight`.
- `-Configuration`: `Development` (por defecto), `DebugGame` o `Shipping` (el editor no admite `Shipping`).
- Log: `Saved/Logs/build.log`. Devuelve el código de salida de UBT.

### `test.ps1`

Ejecuta las Automation Specs sin render y resume el informe.

```powershell
Tools/Scripts/test.ps1                           # todos los tests LeftRight.
Tools/Scripts/test.ps1 -Filter LeftRight.Core.   # solo los de LRCore
```

- Lanza `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests <Filter>; Quit" -unattended -nopause -nullrhi -NoSound -nosplash -log -ReportExportPath=Saved/TestReports`.
- Borra antes `Saved/TestReports`, lee después `Saved/TestReports/index.json` y muestra cuántos tests pasaron y fallaron, y los errores de cada test fallido (mensaje, archivo y línea).
- Devuelve 1 si algún test falla, si no se ejecutó ninguno o si no hay informe (el editor no arrancó o se colgó). Log: `Saved/Logs/test.log`.
- Requiere haber compilado antes `LeftRightEditor` con `build.ps1`.

Los tests viven en `Source/<Módulo>/Private/Tests/<Nombre>Spec.cpp` y se llaman `LeftRight.<Área>.<Caso>`.

### `validate-data.ps1`

Valida todos los `Data/**/*.json` contra `Data/Schema` sin abrir el motor. Llama a `python -I Tools/Data/validate_data.py` (modo aislado: sin variables `PYTHON*`, sin site-packages de usuario y sin la carpeta del script en `sys.path`).

```powershell
python -m pip install -r Tools/Data/requirements.txt   # una sola vez
Tools/Scripts/validate-data.ps1
Tools/Scripts/validate-data.ps1 -Strict                 # un tipo sin esquema es un error
```

Comprobaciones del validador (`Tools/Data/validate_data.py`):

1. Cada archivo es JSON UTF-8 válido (se tolera BOM).
2. El sobre cumple `Data/Schema/Envelope.schema.json`.
3. `data` cumple `Data/Schema/<type>.schema.json` (`Draft202012Validator`); si falta el esquema, avisa (o falla con `--strict`).
4. Todos los esquemas de `Data/Schema` son JSON Schema 2020-12 válidos; se admite `$ref` relativo entre ellos.
5. No hay dos archivos con la misma pareja (`type`, `id`).

Ignora `Data/Schema/` y las carpetas que empiezan por `_` o `.`. Opciones directas del script: `--repo-root`, `--data-dir`, `--strict`, `--quiet`. Código de salida 1 si hay errores.

### `run-python.ps1`

Ejecuta un script Python del editor sin interfaz: `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<script>" -unattended`.

```powershell
Tools/Scripts/run-python.ps1 -Script Tools/Editor/create_materials.py
Tools/Scripts/run-python.ps1 -Script Tools/Editor/x.py -ExtraArgs "-NoSound"
```

- `-Script`: ruta absoluta o relativa a la raíz del repositorio. Usa solo scripts revisados de `Tools/Editor/`.
- Requiere que el plugin **Python Editor Script Plugin** esté activado en el proyecto.
- Log: `Saved/Logs/run-python.log`. Devuelve el código de salida del editor.

Los `.uasset` son `lockable`: quedan en solo lectura hasta hacer `git lfs lock <ruta>`, y sin ese bloqueo Unreal no puede guardar los cambios. Por eso hay que bloquear los materiales antes de ejecutar `import_generated_cars.py` y desbloquearlos después del push.

`Tools/Editor/build_traffic_lods.py` es la excepción: `set_lods` no funciona en un commandlet, así que necesita el editor completo. Hay que ejecutarlo después de `import_generated_cars.py`, porque al reimportar se pierden los LODs:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor.exe" LeftRight.uproject -ExecutePythonScript="$PWD\Tools\Editor\build_traffic_lods.py" -unattended -nosplash -nullrhi
```

### `package.ps1`

Compila, cocina, prepara, empaqueta en `.pak` y archiva una build Win64 con `RunUAT.bat BuildCookRun`.

```powershell
Tools/Scripts/package.ps1                                 # Shipping en Packaged/
Tools/Scripts/package.ps1 -Configuration Development
Tools/Scripts/package.ps1 -ArchiveDirectory D:\Builds\LeftRight
```

- Argumentos: `-project=<uproject> -platform=Win64 -clientconfig=<Config> -build -cook -stage -pak -archive -archivedirectory=<repo>/Packaged -noP4 -utf8output -unattended`.
- Log: `Saved/Logs/package.log` (UAT además guarda sus propios logs en la carpeta del motor). Devuelve el código de salida de UAT.

### `play.ps1`

Lanza el juego sin cocinar (`-game`) en una ventana de 1600×900, con log.

```powershell
Tools/Scripts/play.ps1
Tools/Scripts/play.ps1 -ResX 1920 -ResY 1080
Tools/Scripts/play.ps1 -Wait                    # espera a que se cierre y devuelve su código
```

- Ejecuta `UnrealEditor.exe <uproject> -game -windowed -ResX=1600 -ResY=900 -log`.
- `-ExtraArgs` añade argumentos tal cual (por ejemplo, un mapa o `-ExecCmds=...`).
- Requiere haber compilado `LeftRightEditor`.

## Generadores de contenido

Todo se genera desde código, sin assets ni licencias de terceros. Necesitan Python 3 con numpy, scipy y Pillow.

| Script | Qué genera | Siguiente paso |
|---|---|---|
| `Tools/CarGen/lrcargen.py` | Coches en `.glb` | `Tools/Editor/import_generated_cars.py` y `build_traffic_lods.py` |
| `Tools/SceneGen/lrtexgen.py` | Texturas de asfalto, hormigón y terreno en `Saved/SceneGen/Textures` | `Tools/Editor/create_scene_materials.py` crea `M_LRAsphalt`, `M_LRConcrete` y `M_LRTerrain` |
| `Tools/SceneGen/lrpropgen.py` | Árboles, arbustos, rocas, pórticos con señales y pasos elevados en `Saved/PropGen`, más las texturas de hojas y señales | `create_scene_materials.py` (`M_LRFoliage`, `M_LRSign`) y `Tools/Editor/import_generated_props.py`; las rutas los colocan con su bloque `Props` |
| `Tools/MusicGen/lrmusicgen.py` | Tres temas synthwave en `Data/Music` (AAC) | Nada: el juego los lee del disco. Necesita `ffmpeg` en el PATH |

## Cómo añadir un script nuevo

1. Crea `Tools/Scripts/<nombre>.ps1` con bloque de ayuda (`<# .SYNOPSIS ... #>`), `[CmdletBinding()]` y parámetros validados (`ValidateSet`, `ValidateRange`).
2. Carga `common.ps1` y usa `Get-LRUERoot`, `Get-LRProjectFile`, `Get-LREngineFile` e `Invoke-LRProcess` en lugar de rutas a mano.
3. Compatible con PowerShell 5.1: nada de `&&`, `||`, `??` ni `?:`. Entrecomilla siempre las rutas (pueden tener espacios).
4. Propaga el código de salida con `exit $exitCode` y escribe el log en `Saved/Logs/<nombre>.log`.
5. Documenta el script en este README.

## Compilar en una máquina nueva

`build.ps1` necesita el **SDK de .NET Framework 4.8** (Visual Studio Installer, componente individual `Microsoft.Net.Component.4.8.SDK`). Sin él, UnrealBuildTool falla con `Could not find NetFxSDK install dir` al evaluar el módulo `SwarmInterface`.

## Probar el juego real

`devdrive.ps1 [-Seconds 120] [-ShotEvery 15] [-CycleCameras]` arranca el juego con el piloto automático de desarrollo: conduce solo, reinicia tras cada choque, registra telemetría en el log y guarda capturas y `trace.csv` en `Saved/DevShots`.
