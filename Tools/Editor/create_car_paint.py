"""Creates the car paint master material (idempotent).

Output: /Game/LeftRight/Materials/M_LRCarPaint, clear coat shading model.
Parameters: PaintColor (vector), Metallic, Roughness, ClearCoatRoughness, FlakeTiling, FlakeStrength (scalars).

Python does not expose the clear coat pins of a material directly, so everything goes through a
MakeMaterialAttributes node, which does have them.
"""
import unreal

PACKAGE_PATH = "/Game/LeftRight/Materials"
ASSET_NAME = "M_LRCarPaint"
FLAKES = "/Game/ConceptCar/Car/Assets/T_CarPaintFlakes_N"

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
full_path = f"{PACKAGE_PATH}/{ASSET_NAME}"

if assets.does_asset_exist(full_path):
    material = assets.load_asset(full_path)
    lib.delete_all_material_expressions(material)
else:
    material = tools.create_asset(ASSET_NAME, PACKAGE_PATH, unreal.Material, unreal.MaterialFactoryNew())

material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
material.set_editor_property("use_material_attributes", True)
# Seen from the cockpit, pillars and roof are the back of the shell.
material.set_editor_property("two_sided", True)


def expr(cls, x, y):
    return lib.create_material_expression(material, cls, x, y)


def scalar(name, default, x, y):
    node = expr(unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


attrs = expr(unreal.MaterialExpressionMakeMaterialAttributes, -100, 0)

color = expr(unreal.MaterialExpressionVectorParameter, -500, -300)
color.set_editor_property("parameter_name", "PaintColor")
color.set_editor_property("default_value", unreal.LinearColor(0.55, 0.03, 0.02, 1.0))
lib.connect_material_expressions(color, "", attrs, "BaseColor")

lib.connect_material_expressions(scalar("Metallic", 0.55, -500, -120), "", attrs, "Metallic")
lib.connect_material_expressions(scalar("Roughness", 0.34, -500, -20), "", attrs, "Roughness")

clear = expr(unreal.MaterialExpressionConstant, -500, 80)
clear.set_editor_property("r", 1.0)
lib.connect_material_expressions(clear, "", attrs, "ClearCoat")
lib.connect_material_expressions(scalar("ClearCoatRoughness", 0.035, -500, 180), "", attrs, "ClearCoatRoughness")

# Metallic flakes: fine normal noise under the smooth clear coat.
coords = expr(unreal.MaterialExpressionTextureCoordinate, -1100, 300)
tiling = scalar("FlakeTiling", 45.0, -1100, 420)
scaled = expr(unreal.MaterialExpressionMultiply, -900, 340)
lib.connect_material_expressions(coords, "", scaled, "A")
lib.connect_material_expressions(tiling, "", scaled, "B")
sample = expr(unreal.MaterialExpressionTextureSample, -700, 300)
sample.set_editor_property("texture", assets.load_asset(FLAKES))
sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
lib.connect_material_expressions(scaled, "", sample, "UVs")
flat = expr(unreal.MaterialExpressionConstant3Vector, -700, 520)
flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
blend = expr(unreal.MaterialExpressionLinearInterpolate, -350, 340)
lib.connect_material_expressions(flat, "", blend, "A")
lib.connect_material_expressions(sample, "", blend, "B")
lib.connect_material_expressions(scalar("FlakeStrength", 0.12, -600, 640), "", blend, "Alpha")
lib.connect_material_expressions(blend, "", attrs, "Normal")

lib.connect_material_property(attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)

lib.recompile_material(material)
assets.save_loaded_asset(material)
unreal.log(f"LRMaterials: created {full_path}")
