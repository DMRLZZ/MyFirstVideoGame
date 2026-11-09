"""Imports the roadside objects of Tools/SceneGen/lrpropgen.py into /Game/LeftRight/Props and assigns their materials.

1. python Tools/SceneGen/lrpropgen.py --out Saved/PropGen --textures Saved/SceneGen/Textures
2. Tools/Editor/create_scene_materials.py (with LR_ONLY=T_Leaves_D,T_Signs_D,M_LRFoliage,M_LRSign the first time)
3. Tools/Scripts/run-python.ps1 -Script Tools/Editor/import_generated_props.py

Idempotent. Existing assets are lockable: delete Content/LeftRight/Props or `git lfs lock` them before a new import.
"""
import glob
import os

import unreal

SOURCE_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "PropGen")
DEST = "/Game/LeftRight/Props"
MATERIAL_DIR = "/Game/LeftRight/Materials/Props"
SURFACE = "/Game/LeftRight/Materials/M_LRSurface"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def surface_instance(name, color, roughness, metallic=0.0, grain=0.0, grain_scale=4.0):
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = tools.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", assets.load_asset(SURFACE))
    lib.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(*color, 1.0))
    lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
    lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)
    lib.set_material_instance_scalar_parameter_value(instance, "GrainAmount", grain)
    lib.set_material_instance_scalar_parameter_value(instance, "GrainScale", grain_scale)
    if not assets.save_loaded_asset(instance, only_if_is_dirty=False):
        unreal.log_error(f"LRProps: could not save {path}, is it locked with git lfs lock?")
    return instance


SLOTS = {
    "Bark": surface_instance("MI_Prop_Bark", (0.07, 0.055, 0.045), 0.9, grain=0.6, grain_scale=6.0),
    "Rock": surface_instance("MI_Prop_Rock", (0.2, 0.17, 0.13), 0.85, grain=0.7, grain_scale=3.0),
    "Metal": surface_instance("MI_Prop_Metal", (0.32, 0.33, 0.34), 0.45, metallic=1.0, grain=0.15),
    "SignBack": surface_instance("MI_Prop_SignBack", (0.18, 0.19, 0.2), 0.6, metallic=1.0),
    "Leaf": assets.load_asset("/Game/LeftRight/Materials/M_LRFoliage"),
    "Sign": assets.load_asset("/Game/LeftRight/Materials/M_LRSign"),
    "Concrete": assets.load_asset("/Game/LeftRight/Materials/M_LRConcrete"),
    "Earth": assets.load_asset("/Game/LeftRight/Materials/M_LRTerrain"),
}

for source in sorted(glob.glob(os.path.join(SOURCE_DIR, "*.glb"))):
    name = os.path.splitext(os.path.basename(source))[0]
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", f"{DEST}/{name}")
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])

    for path in assets.list_assets(f"{DEST}/{name}", recursive=True):
        asset = assets.load_asset(path)
        if not isinstance(asset, unreal.StaticMesh):
            continue
        for index, slot in enumerate(asset.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            if slot_name in SLOTS and SLOTS[slot_name] is not None:
                asset.set_material(index, SLOTS[slot_name])
            else:
                unreal.log_warning(f"LRProps: unknown slot {slot_name} on {path}")
        # Plain triangles, like the cars: the Nanite fallback of thin cards and small meshes looks worse.
        nanite = asset.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", False)
        asset.set_editor_property("nanite_settings", nanite)
        # Instanced by the hundred and never touched: no collision.
        body = asset.get_editor_property("body_setup")
        if body is not None:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
            unreal.log_error(f"LRProps: could not save {path}")
        unreal.log(f"LRProps: imported {path}")
