"""Imports the cars produced by Tools/CarGen into /Game/LeftRight/Vehicles and assigns their materials (idempotent).

1. python -I Tools/CarGen/lrcargen.py Tools/CarGen/designs/<id>.json --glb Saved/CarGen
2. Tools/Scripts/run-python.ps1 -Script Tools/Editor/import_generated_cars.py
"""
import glob
import os

import unreal

SOURCE_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "CarGen")
DEST = "/Game/LeftRight/Vehicles/Generated"
MATERIAL_DIR = "/Game/LeftRight/Materials/Car"
SURFACE = "/Game/LeftRight/Materials/M_LRSurface"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def surface_instance(name, color, roughness, metallic=0.0, specular=0.5, grain=0.0, emissive=None):
    path = f"{MATERIAL_DIR}/{name}"
    if assets.does_asset_exist(path):
        instance = assets.load_asset(path)
    else:
        instance = tools.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", assets.load_asset(SURFACE))
    lib = unreal.MaterialEditingLibrary
    lib.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(*color, 1.0))
    lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
    lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)
    lib.set_material_instance_scalar_parameter_value(instance, "Specular", specular)
    lib.set_material_instance_scalar_parameter_value(instance, "GrainAmount", grain)
    if emissive:
        lib.set_material_instance_vector_parameter_value(instance, "Emissive", unreal.LinearColor(*emissive, 1.0))
    # Setting a parameter does not mark the package dirty: without this, an existing instance keeps its old values.
    # Saving fails on a read-only file, which is what an unlocked lockable asset is: run `git lfs lock` on it first.
    if not assets.save_loaded_asset(instance, only_if_is_dirty=False):
        unreal.log_error(f"LRCars: could not save {path}, is it locked with git lfs lock?")
    return instance


SLOTS = {
    "Paint": assets.load_asset("/Game/LeftRight/Materials/M_LRCarPaint"),
    "Glass": assets.load_asset("/Game/ConceptCar/Car/Assets/M_Glass"),
    "Metal": assets.load_asset("/Game/ConceptCar/Car/Assets/M_Metal"),
    "Tire": surface_instance("MI_Car_Tire", (0.01, 0.01, 0.011), 0.82, specular=0.25, grain=0.2),
    "Trim": surface_instance("MI_Car_Trim", (0.006, 0.006, 0.007), 0.55, specular=0.3, grain=0.15),
    "Gap": surface_instance("MI_Car_Gap", (0.002, 0.002, 0.002), 0.9),
    "Interior": surface_instance("MI_Car_Interior", (0.04, 0.034, 0.03), 0.7, grain=0.3),
    # Gunmetal: brighter metal mirrors the sky at sunset and the spokes read as flat white.
    "Rim": surface_instance("MI_Car_Rim", (0.07, 0.072, 0.08), 0.3, metallic=1.0),
    "Brake": surface_instance("MI_Car_Brake", (0.25, 0.25, 0.26), 0.4, metallic=1.0),
    # Very low specular also lowers the reflection at grazing angles, where a lens this flat mirrors the white horizon.
    "HeadLight": surface_instance("MI_Car_HeadLight", (0.02, 0.022, 0.026), 0.12, specular=0.12, emissive=(0.02, 0.02, 0.025)),
    "DRL": surface_instance("MI_Car_DRL", (0.9, 0.92, 1.0), 0.3, emissive=(14.0, 14.5, 16.0)),
    "TailLight": surface_instance("MI_Car_TailLight", (0.09, 0.003, 0.004), 0.1, emissive=(0.03, 0.0, 0.0)),
    "TailLED": surface_instance("MI_Car_TailLED", (0.6, 0.02, 0.02), 0.3, emissive=(5.0, 0.12, 0.07)),
    "Screen": surface_instance("MI_Car_Screen", (0.01, 0.015, 0.02), 0.2, emissive=(0.5, 0.75, 1.0)),
    "Caliper": surface_instance("MI_Car_Caliper", (0.6, 0.03, 0.02), 0.35, specular=0.6),
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
    unreal.log(f"LRCars: imported {name}: {list(task.get_editor_property('imported_object_paths'))}")

    for path in assets.list_assets(f"{DEST}/{name}", recursive=True):
        asset = assets.load_asset(path)
        if not isinstance(asset, unreal.StaticMesh):
            continue
        slots = asset.get_editor_property("static_materials")
        for index, slot in enumerate(slots):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            if slot_name in SLOTS:
                asset.set_material(index, SLOTS[slot_name])
            else:
                unreal.log_warning(f"LRCars: unknown slot {slot_name} on {path}")
        # Plain triangles: the Interchange importer turns Nanite on, and its coarse fallback is what shows on this project.
        nanite = asset.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", False)
        asset.set_editor_property("nanite_settings", nanite)

        # Levels of detail need the full editor: run build_traffic_lods.py after this script.
        bounds = asset.get_bounding_box()
        names = [str(s.get_editor_property("material_slot_name")) for s in slots]
        unreal.log(f"LRCars: {path} slots={names} min={bounds.min} max={bounds.max}")
        assets.save_loaded_asset(asset)

    # The importer also created placeholder materials from the glTF: nothing uses them now.
    junk = f"{DEST}/{name}/{name}/Materials"
    if assets.does_directory_exist(junk):
        assets.delete_directory(junk)

# Physics rig wheel positions, so generated bodies can line up with the rig.
skeleton = assets.load_asset("/Game/Vehicles/SportsCar/SK_SportsCar")
if skeleton:
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    for bone in ("Phys_Wheel_FL", "Phys_Wheel_FR", "Phys_Wheel_BL", "Phys_Wheel_BR"):
        t = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
        unreal.log(f"LRCars: bone {bone} at {t.translation}")
