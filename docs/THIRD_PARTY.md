# Registro de terceros (assets, plugins, audio, fuentes)

Todo lo que no hayamos creado nosotros se registra aquí **antes** de entrar en el repo. Sin registro, no se fusiona. Los plugins y el código que entran en el repo además pasan la revisión de [05-AGENTS.md §7](05-AGENTS.md) y necesitan aprobación humana. Los prerrequisitos del motor que solo viajan con la build empaquetada (por ejemplo, los redistribuibles de Visual C++) se registran aquí con aprobación humana, pero no pasan esa revisión.

| Fecha | Elemento | Tipo | Origen (URL) | Licencia | Versión | Aprobado por | Uso en el proyecto |
|---|---|---|---|---|---|---|---|
| 2026-11-05 | SportsCar de la plantilla de vehículos de UE (malla, esqueleto, física, ABP, materiales, texturas) y PhysicsMaterials | 3d-model | `UE_5.8/Templates/TemplateResources/Standard/Vehicles/Content` (instalación de Unreal Engine) | Contenido de Epic para proyectos Unreal (EULA de Unreal Engine) | UE 5.8.4 | Director del proyecto | Coche **provisional** del prototipo gris en `/Game/Vehicles/`. Se sustituye por coches propios en M3/M4 |
| 2026-11-05 | Coche conceptual y SUV de las plantillas de UE (mallas, materiales, texturas) | 3d-model | `UE_5.8/Templates/TemplateResources/Standard/{ConceptCar,Vehicles}` (instalación de Unreal Engine) | Contenido de Epic para proyectos Unreal (EULA de Unreal Engine) | UE 5.8.4 | Director del proyecto | Tráfico **provisional** del prototipo en `/Game/ConceptCar/` y `/Game/Vehicles/OffroadCar/` |
| 2026-11-10 | Redistribuibles de Microsoft Visual C++ (`vc_redist.x64.exe` y `vc_redist.arm64.exe`) | code | `UE_5.8/Engine/Extras/Redist/en-us` (instalación de Unreal Engine) | Términos de licencia del redistribuible de Microsoft Visual C++ (se muestran al instalarlo) | 14.50.35719 | Director del proyecto | `package.ps1` los copia en la build con `-prereqs` (`<archivo>/Windows/Engine/Extras/Redist/en-us/`). El lanzador `LeftRight.exe` los ejecuta si falta el runtime. **No entran en el repo.** Revisar los términos antes de publicar en Steam (M7) |

**Tipos:** `plugin`, `code`, `3d-model`, `material`, `texture`, `audio`, `music`, `font`, `hdri`.

**Prohibido:** modelos con licencia "Editorial", assets con marcas reales de coches sin contrato, y música sin licencia para streaming.
