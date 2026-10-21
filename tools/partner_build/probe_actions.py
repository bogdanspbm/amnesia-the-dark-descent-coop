# Frame rate, root motion and lantern hand of the actions the partner will use.
import bpy

Q = r"E:\Programs\Blender\assets\quaternius"
CHAR = Q + r"\universal_base_characters\Universal Base Characters[Standard]\Base Characters\Unity\Superhero_Male_FullBody.fbx"
UAL2 = Q + r"\universal_animation_library_2\Universal Animation Library 2[Standard]\Unity\UAL2_Standard.fbx"
UAL1 = Q + r"\universal_animation_library\Animation Library[Standard]\Unreal Engine\AL_Standard.fbx"
WANT = ["Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Crouch_Idle_Loop", "Crouch_Fwd_Loop", "Idle_Lantern_Loop", "Idle_Torch_Loop", "Hit_Chest", "Death01"]

bpy.ops.wm.read_factory_settings(use_empty=True)
for p in (CHAR, UAL2, UAL1):
    bpy.ops.import_scene.fbx(filepath=p)
    print("fps after", p.split("\\")[-1], bpy.context.scene.render.fps, bpy.context.scene.render.fps_base)

arm = next(o for o in bpy.data.objects if o.type == "ARMATURE" and not o.animation_data)  # the character
if arm.animation_data is None: arm.animation_data_create()
print("char armature", arm.name, "has", len(arm.data.bones), "bones")

def act_by_suffix(s):
    return [a for a in bpy.data.actions if a.name.split("|")[-1] == s]

scene = bpy.context.scene
def sample(act, frame, bones):
    ad = arm.animation_data
    ad.action = act
    if hasattr(ad, "action_slot") and act.slots: ad.action_slot = act.slots[0]
    scene.frame_set(frame)
    return {b: (arm.matrix_world @ arm.pose.bones[b].head).copy() for b in bones}

for s in WANT:
    acts = act_by_suffix(s)
    if not acts: print(s, "MISSING"); continue
    a = acts[0]
    f0, f1 = int(a.frame_range[0]), int(a.frame_range[1])
    p0 = sample(a, f0, ["root", "pelvis", "hand_l", "hand_r"])
    p1 = sample(a, f1, ["root", "pelvis", "hand_l", "hand_r"])
    d_root = p1["root"] - p0["root"]; d_pel = p1["pelvis"] - p0["pelvis"]
    print(f"{s}: {len(acts)} copies, frames {f0}-{f1}, root moves {tuple(round(v,3) for v in d_root)}, pelvis moves {tuple(round(v,3) for v in d_pel)}, pelvis z {p0['pelvis'].z:.3f}")
    print(f"   hand_l {tuple(round(v,2) for v in p0['hand_l'])} hand_r {tuple(round(v,2) for v in p0['hand_r'])}")
