"""Imports the generated world textures and builds the master materials of the road and the terrain (idempotent).

Run Tools/SceneGen/lrtexgen.py first, then: Tools/Scripts/run-python.ps1 -Script Tools/Editor/create_scene_materials.py
The assets are lockable: to run it again over existing assets, `git lfs lock` them first or the save fails.

Run Tools/SceneGen/lrpropgen.py too: it writes the leaf and sign textures.
LR_ONLY (environment variable, comma separated) limits the run to some assets, e.g. LR_ONLY=T_Leaves_D,M_LRFoliage,
so the assets that did not change are not saved again.

Output in /Game/LeftRight/Materials, used from C++ through dynamic instances:
  M_LRAsphalt   road surface, UVs in meters. Adds wheel tracks from LaneOrigin and LanePeriod.
  M_LRConcrete  barriers and structures, UVs in meters.
  M_LRTerrain   ground and hills, projected from above in world space. ColorB tints large patches.
  M_LRFoliage   leaf cards: masked, two-sided, tinted per instance between Color and ColorB.
  M_LRSign      guide signs, with a little glow for the retroreflective sheeting.
All of them have a "Color" parameter that multiplies the texture.
"""
import os

import unreal

SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "SceneGen", "Textures")
TEXTURES = "/Game/LeftRight/Textures"
MATERIALS = "/Game/LeftRight/Materials"

ONLY = {name.strip() for name in os.environ.get("LR_ONLY", "").split(",") if name.strip()}


def wanted(name):
    return not ONLY or name in ONLY


assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def import_texture(name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SOURCE, f"{name}.png"))
    task.set_editor_property("destination_path", TEXTURES)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    tools.import_asset_tasks([task])
    texture = assets.load_asset(f"{TEXTURES}/{name}")
    if name.endswith("_N"):
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif name.endswith("_R"):
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    elif name.endswith("_M"):
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
        unreal.log_error(f"LRScene: could not save {name}, is it locked with git lfs lock?")
    return texture


class Graph:
    """Small helper over MaterialEditingLibrary: every call creates one node."""

    def __init__(self, name):
        path = f"{MATERIALS}/{name}"
        if assets.does_asset_exist(path):
            self.material = assets.load_asset(path)
            lib.delete_all_material_expressions(self.material)
        else:
            self.material = tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
        self.name = name
        self.x = -1800
        # The game cannot add usage flags at runtime: without this, instanced props fall back to the default material.
        self.material.set_editor_property("used_with_instanced_static_meshes", True)

    def node(self, cls, **properties):
        self.x += 8
        expression = lib.create_material_expression(self.material, cls, self.x, 0)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def scalar(self, name, default):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)

    def vector(self, name, default):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*default, 1.0))

    def constant(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=value)

    def binary(self, cls, a, b):
        expression = self.node(cls)
        lib.connect_material_expressions(a, "", expression, "A")
        lib.connect_material_expressions(b, "", expression, "B")
        return expression

    def mul(self, a, b):
        return self.binary(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.binary(unreal.MaterialExpressionAdd, a, b)

    def sub(self, a, b):
        return self.binary(unreal.MaterialExpressionSubtract, a, b)

    def div(self, a, b):
        return self.binary(unreal.MaterialExpressionDivide, a, b)

    def lerp(self, a, b, alpha):
        expression = self.node(unreal.MaterialExpressionLinearInterpolate)
        lib.connect_material_expressions(a, "", expression, "A")
        lib.connect_material_expressions(b, "", expression, "B")
        lib.connect_material_expressions(alpha, "", expression, "Alpha")
        return expression

    def texture(self, parameter, texture, uv, sampler):
        expression = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=parameter, texture=texture, sampler_type=sampler)
        lib.connect_material_expressions(uv, "", expression, "UVs")
        return expression

    def channel(self, source, output):
        """A one-channel view of a texture node: passes through a reroute-free multiply by one."""
        expression = self.node(unreal.MaterialExpressionMultiply, const_b=1.0)
        lib.connect_material_expressions(source, output, expression, "A")
        return expression

    def finish(self, base, normal, roughness, specular=0.4):
        lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
        lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        lib.connect_material_property(self.constant(specular), "", unreal.MaterialProperty.MP_SPECULAR)
        lib.recompile_material(self.material)
        if not assets.save_loaded_asset(self.material, only_if_is_dirty=False):
            unreal.log_error(f"LRScene: could not save {self.name}, is it locked with git lfs lock?")
        unreal.log(f"LRScene: built {self.name}")


COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
GRAY = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE
MASKS = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS


def layered_color(g, tex, uv, tile):
    """Sampled at two unrelated scales and mixed: the repetition of one tile stops being visible."""
    fine = g.mul(uv, g.scalar("TileScale", tile))
    broad = g.mul(uv, g.constant(tile * 0.173))
    a = g.texture("BaseTex", tex, fine, COLOR)
    b = g.texture("BaseTexBroad", tex, broad, COLOR)
    return fine, g.lerp(a, b, g.constant(0.4))


def around_one(g, value, amount):
    """1 + (value - 0.5) * amount: darkens and lightens around the original color."""
    return g.add(g.constant(1.0), g.mul(g.sub(value, g.constant(0.5)), amount))


def build_asphalt(textures):
    g = Graph("M_LRAsphalt")
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    fine, color = layered_color(g, textures["T_Asphalt_D"], uv, 0.25)
    macro = g.texture("MacroTex", textures["T_Macro_M"], g.mul(uv, g.constant(0.016)), MASKS)

    # Two wheel tracks per lane: a cosine of the lateral position with half a lane as its period.
    across = g.node(unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    lib.connect_material_expressions(uv, "", across, "")
    phase = g.div(g.sub(across, g.scalar("LaneOrigin", 0.0)), g.scalar("LanePeriod", 1.85))
    cosine = g.node(unreal.MaterialExpressionCosine, period=1.0)
    lib.connect_material_expressions(phase, "", cosine, "")
    clamped = g.node(unreal.MaterialExpressionSaturate)
    lib.connect_material_expressions(cosine, "", clamped, "")
    power = g.node(unreal.MaterialExpressionPower, const_exponent=3.0)
    lib.connect_material_expressions(clamped, "", power, "Base")
    wear = g.mul(power, g.scalar("WearAmount", 0.3))

    tracks = g.sub(g.constant(1.0), wear)
    cracks = g.sub(g.constant(1.0), g.mul(g.channel(macro, "G"), g.constant(0.75)))
    repairs = g.sub(g.constant(1.0), g.mul(g.channel(macro, "B"), g.constant(0.35)))
    mottling = around_one(g, g.channel(macro, "R"), g.constant(0.45))
    shade = g.mul(g.mul(tracks, cracks), g.mul(repairs, mottling))
    tint = g.vector("Color", (1.0, 1.0, 1.0))
    base = g.mul(g.mul(color, tint), shade)

    normal = g.texture("NormalTex", textures["T_Asphalt_N"], fine, NORMAL)
    rough = g.texture("RoughTex", textures["T_Asphalt_R"], fine, GRAY)
    roughness = g.sub(rough, g.mul(wear, g.constant(0.4)))
    g.finish(base, normal, roughness, specular=0.35)


def build_concrete(textures):
    g = Graph("M_LRConcrete")
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    fine, color = layered_color(g, textures["T_Concrete_D"], uv, 0.25)
    macro = g.texture("MacroTex", textures["T_Macro_M"], g.mul(uv, g.constant(0.016)), MASKS)
    mottling = around_one(g, g.channel(macro, "R"), g.constant(0.5))
    base = g.mul(g.mul(color, g.vector("Color", (1.0, 1.0, 1.0))), mottling)
    normal = g.texture("NormalTex", textures["T_Concrete_N"], fine, NORMAL)
    rough = g.texture("RoughTex", textures["T_Concrete_R"], fine, GRAY)
    g.finish(base, normal, rough, specular=0.4)


def build_terrain(textures):
    g = Graph("M_LRTerrain")
    world = g.node(unreal.MaterialExpressionWorldPosition)
    mask = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=False, a=False)
    lib.connect_material_expressions(world, "", mask, "")
    uv = g.div(mask, g.constant(100.0))  # Centimeters to meters.
    fine, color = layered_color(g, textures["T_Terrain_D"], uv, 0.125)
    macro = g.texture("MacroTex", textures["T_Macro_M"], g.mul(uv, g.constant(0.0091)), MASKS)
    hills = g.texture("HillsTex", textures["T_Macro_M"], g.mul(uv, g.constant(0.00073)), MASKS)
    mottling = around_one(g, g.channel(macro, "R"), g.constant(0.7))
    patches = g.node(unreal.MaterialExpressionSmoothStep, const_min=0.35, const_max=0.65)
    lib.connect_material_expressions(g.channel(hills, "R"), "", patches, "Value")
    tint = g.lerp(g.vector("Color", (1.0, 1.0, 1.0)), g.vector("ColorB", (0.42, 0.5, 0.3)), patches)
    base = g.mul(g.mul(color, tint), mottling)
    normal = g.texture("NormalTex", textures["T_Terrain_N"], fine, NORMAL)
    rough = g.texture("RoughTex", textures["T_Terrain_R"], fine, GRAY)
    g.finish(base, normal, rough, specular=0.3)


def build_foliage(textures):
    g = Graph("M_LRFoliage")
    g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    g.material.set_editor_property("two_sided", True)
    g.material.set_editor_property("opacity_mask_clip_value", 0.45)
    leaves = g.texture("BaseTex", textures["T_Leaves_D"], g.node(unreal.MaterialExpressionTextureCoordinate), COLOR)
    random = g.node(unreal.MaterialExpressionPerInstanceRandom)
    tint = g.lerp(g.vector("Color", (0.42, 0.5, 0.17)), g.vector("ColorB", (0.75, 0.58, 0.24)), g.mul(g.mul(random, random), g.constant(0.7)))
    lib.connect_material_property(g.mul(leaves, tint), "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(leaves, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    lib.connect_material_property(g.constant(0.75), "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(g.constant(0.3), "", unreal.MaterialProperty.MP_SPECULAR)
    save_material(g)


def build_sign(textures):
    g = Graph("M_LRSign")
    g.material.set_editor_property("two_sided", True)
    sign = g.texture("BaseTex", textures["T_Signs_D"], g.node(unreal.MaterialExpressionTextureCoordinate), COLOR)
    lib.connect_material_property(sign, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(g.mul(sign, g.scalar("Glow", 0.25)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(g.constant(0.5), "", unreal.MaterialProperty.MP_ROUGHNESS)
    save_material(g)


def save_material(g):
    lib.recompile_material(g.material)
    if not assets.save_loaded_asset(g.material, only_if_is_dirty=False):
        unreal.log_error(f"LRScene: could not save {g.name}, is it locked with git lfs lock?")
    unreal.log(f"LRScene: built {g.name}")


names = ["T_Asphalt_D", "T_Asphalt_N", "T_Asphalt_R", "T_Concrete_D", "T_Concrete_N", "T_Concrete_R",
         "T_Terrain_D", "T_Terrain_N", "T_Terrain_R", "T_Macro_M", "T_Leaves_D", "T_Signs_D"]
loaded = {name: import_texture(name) if wanted(name) else assets.load_asset(f"{TEXTURES}/{name}") for name in names}
for material, build in (("M_LRAsphalt", build_asphalt), ("M_LRConcrete", build_concrete), ("M_LRTerrain", build_terrain),
                        ("M_LRFoliage", build_foliage), ("M_LRSign", build_sign)):
    if wanted(material):
        build(loaded)
