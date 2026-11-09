"""Creates the master material for road and roadside surfaces (idempotent).

Run with: Tools/Scripts/run-python.ps1 -Script Tools/Editor/create_road_materials.py

Output: /Game/LeftRight/Materials/M_LRSurface
Parameters used from C++ through dynamic instances:
  Color (vector)      base color
  Roughness (scalar)  surface roughness
  Specular (scalar)   dielectric specular
  GrainAmount (scalar) strength of the procedural aggregate noise
  GrainScale (scalar) noise frequency, repetitions per meter
  Metallic (scalar)   0 = dielectric, 1 = metal
  Emissive (vector)   self illumination (lamps), linear color * intensity
UVs are world meters (see ALRRoadStreamer), so the grain has the same size everywhere.
"""
import unreal

PACKAGE_PATH = "/Game/LeftRight/Materials"
ASSET_NAME = "M_LRSurface"

lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
full_path = f"{PACKAGE_PATH}/{ASSET_NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(full_path):
    material = unreal.EditorAssetLibrary.load_asset(full_path)
    lib.delete_all_material_expressions(material)
else:
    material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material, unreal.MaterialFactoryNew())

# The game cannot add usage flags at runtime: instanced roadside objects (lamps, bark, rocks) need this one.
material.set_editor_property("used_with_instanced_static_meshes", True)


def expr(cls, x, y):
    return lib.create_material_expression(material, cls, x, y)


def vector_param(name, default, x, y):
    node = expr(unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def scalar_param(name, default, x, y):
    node = expr(unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


color = vector_param("Color", unreal.LinearColor(0.035, 0.035, 0.038, 1.0), -900, -200)
roughness = scalar_param("Roughness", 0.9, -500, 200)
specular = scalar_param("Specular", 0.25, -500, 320)
grain_amount = scalar_param("GrainAmount", 0.5, -900, 100)
grain_scale = scalar_param("GrainScale", 8.0, -1300, 0)

# Fine aggregate: UVs (meters) * scale drive a tiling noise.
coords = expr(unreal.MaterialExpressionTextureCoordinate, -1300, -150)
scaled = expr(unreal.MaterialExpressionMultiply, -1100, -80)
lib.connect_material_expressions(coords, "", scaled, "A")
lib.connect_material_expressions(grain_scale, "", scaled, "B")

noise = expr(unreal.MaterialExpressionNoise, -900, -60)
noise.set_editor_property("scale", 1.0)
noise.set_editor_property("levels", 3)
noise.set_editor_property("output_min", 0.0)
noise.set_editor_property("output_max", 1.0)
lib.connect_material_expressions(scaled, "", noise, "Position")

# 1 +- grain: darkens and lightens the base color around its value.
blend = expr(unreal.MaterialExpressionLinearInterpolate, -600, -20)
one = expr(unreal.MaterialExpressionConstant, -800, 30)
one.set_editor_property("r", 1.0)
lib.connect_material_expressions(one, "", blend, "A")
lib.connect_material_expressions(noise, "", blend, "B")
lib.connect_material_expressions(grain_amount, "", blend, "Alpha")

base = expr(unreal.MaterialExpressionMultiply, -300, -120)
lib.connect_material_expressions(color, "", base, "A")
lib.connect_material_expressions(blend, "", base, "B")

lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
lib.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)

metallic = scalar_param("Metallic", 0.0, -500, 440)
lib.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

emissive = vector_param("Emissive", unreal.LinearColor(0.0, 0.0, 0.0, 1.0), -500, 560)
lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

lib.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
unreal.log(f"LRMaterials: created {full_path}")
