"""Adds levels of detail to the generated car meshes that appear many times on screen.

The traffic can show about sixty cars at once, most of them far away, so their meshes need
simpler versions. `set_lods` refuses to run in a commandlet (it returns -1), which is why this
script is separate from `import_generated_cars.py`: it needs the full editor.

    UnrealEditor.exe LeftRight.uproject -ExecutePythonScript=<this file> -unattended -nosplash -nullrhi

It is idempotent: running it again replaces the levels, and it closes the editor when done.
"""
import unreal

VEHICLE_DIR = "/Game/LeftRight/Vehicles/Generated"

# (share of the triangles of the source mesh, screen size from which the level is used)
LEVELS = {
    "_Traffic": ((1.0, 1.0), (0.4, 0.35), (0.12, 0.14), (0.03, 0.05)),
    "_Wheel": ((1.0, 1.0), (0.35, 0.08), (0.08, 0.03)),
}


def make_options(levels):
    options = unreal.EditorScriptingMeshReductionOptions()
    settings = []
    for percent, screen_size in levels:
        level = unreal.EditorScriptingMeshReductionSettings()
        level.set_editor_property("percent_triangles", percent)
        level.set_editor_property("screen_size", screen_size)
        settings.append(level)
    options.set_editor_property("reduction_settings", settings)
    options.set_editor_property("auto_compute_lod_screen_size", False)
    return options


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(VEHICLE_DIR, recursive=True)
    for data in assets:
        name = str(data.asset_name)
        levels = next((value for suffix, value in LEVELS.items() if name.endswith(suffix)), None)
        if levels is None or not name.startswith("SM_"):
            continue
        mesh = data.get_asset()
        count = unreal.EditorStaticMeshLibrary.set_lods(mesh, make_options(levels))
        vertices = [unreal.EditorStaticMeshLibrary.get_number_verts(mesh, lod) for lod in range(max(count, 0))]
        unreal.log(f"LRLods: {name} levels={count} vertices={vertices}")
        if count <= 0:
            unreal.log_error(f"LRLods: could not build the levels of {name}")
            continue
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)


try:
    main()
finally:
    unreal.SystemLibrary.quit_editor()
