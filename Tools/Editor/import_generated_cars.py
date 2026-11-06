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
    assets.save_loaded_asset(instance)
    return instance


SLOTS = {
    "Paint": assets.load_asset("/Game/ConceptCar/Car/Assets/MI_CarPaint"),
    "Glass": assets.load_asset("/Game/ConceptCar/Car/Assets/M_Glass"),
    "Metal": assets.load_asset("/Game/ConceptCar/Car/Assets/M_Metal"),
    "Tire": assets.load_asset("/Game/ConceptCar/Car/Assets/M_TireRubber"),
    "Trim": surface_instance("MI_Car_Trim", (0.012, 0.012, 0.013), 0.45, grain=0.15),
    "Gap": surface_instance("MI_Car_Gap", (0.002, 0.002, 0.002), 0.9),
    "Interior": surface_instance("MI_Car_Interior", (0.02, 0.016, 0.014), 0.7, grain=0.3),
    "Rim": surface_instance("MI_Car_Rim", (0.55, 0.56, 0.58), 0.28, metallic=1.0),
    "Brake": surface_instance("MI_Car_Brake", (0.25, 0.25, 0.26), 0.4, metallic=1.0),
    "HeadLight": surface_instance("MI_Car_HeadLight", (0.8, 0.82, 0.85), 0.12, emissive=(6.0, 6.2, 6.8)),
    "TailLight": surface_instance("MI_Car_TailLight", (0.25, 0.005, 0.005), 0.18, emissive=(1.4, 0.03, 0.02)),
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
