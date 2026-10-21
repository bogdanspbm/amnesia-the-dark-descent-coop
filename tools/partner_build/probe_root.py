# Which bone carries the 180 degree turn between UAL1 (Unreal FBX) and UAL2 (Unity FBX) actions.
import bpy

Q = r"E:\Programs\Blender\assets\quaternius"
CHAR = Q + r"\universal_base_characters\Universal Base Characters[Standard]\Base Characters\Unity\Superhero_Male_FullBody.fbx"
UAL2 = Q + r"\universal_animation_library_2\Universal Animation Library 2[Standard]\Unity\UAL2_Standard.fbx"
UAL1 = Q + r"\universal_animation_library\Animation Library[Standard]\Unreal Engine\AL_Standard.fbx"

bpy.ops.wm.read_factory_settings(use_empty=True)
for p in (CHAR, UAL2, UAL1):
    bpy.ops.import_scene.fbx(filepath=p)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE" and not o.animation_data)
arm.animation_data_create()
scene = bpy.context.scene

def show(suffix):
    act = next(a for a in bpy.data.actions if a.name.split("|")[-1] == suffix)
    ad = arm.animation_data; ad.action = act
    if act.slots: ad.action_slot = act.slots[0]
    scene.frame_set(1)
    for b in ("root", "pelvis", "spine_01"):
        pb = arm.pose.bones[b]
        q = pb.rotation_quaternion if pb.rotation_mode == "QUATERNION" else pb.rotation_euler.to_quaternion()
        print(f"  {suffix:20s} {b:8s} local q {tuple(round(v,3) for v in q)} loc {tuple(round(v,3) for v in pb.location)} armspace fwd {tuple(round(v,2) for v in (pb.matrix.to_3x3().col[1]))}")
    # fcurve owners
    names = set()
    for layer in act.layers:
        for strip in layer.strips:
            for cb in strip.channelbags:
                for fc in cb.fcurves:
                    names.add(fc.data_path.split('"')[1] if '"' in fc.data_path else fc.data_path)
    print(f"  {suffix}: animates root={('root' in names)} bones={len(names)} sample paths: {sorted(names)[:3]}")

for s in ("Idle_Loop", "Walk_Loop", "Idle_Lantern_Loop", "Idle_FoldArms_Loop"):
    show(s)
