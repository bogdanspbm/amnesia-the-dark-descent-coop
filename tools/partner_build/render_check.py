# Renders the built partner in a few poses for a visual check.
#   blender.exe -b coop_partner.blend --python render_check.py
import bpy, os, math
from mathutils import Vector

OUTDIR = os.path.join(os.path.dirname(bpy.data.filepath), "renders")
os.makedirs(OUTDIR, exist_ok=True)
scene = bpy.context.scene
arm = bpy.data.objects["coop_partner_rig"]
for o in bpy.data.objects:
    if o.name == "coop_partner_lantern": o.hide_render = True
    if o.name == "lantern_preview": o.hide_render = False

scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE"
scene.render.resolution_x, scene.render.resolution_y = 640, 900
world = bpy.data.worlds.new("w"); scene.world = world

cam_data = bpy.data.cameras.new("cam"); cam_data.lens = 50
cam = bpy.data.objects.new("cam", cam_data); scene.collection.objects.link(cam); scene.camera = cam

# Show the exported textures: each material gets an image node with its .png from _coop\partner
PARTNER = os.path.join(os.path.dirname(os.path.dirname(bpy.data.filepath)), "partner")
for m in bpy.data.materials:
    path = os.path.join(PARTNER, m.name + ".png")
    if not os.path.exists(path): continue
    m.use_nodes = True
    nt = m.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = bpy.data.images.load(path, check_existing=True)
    nt.nodes.active = tex

def look(cam, target, dist, yaw_deg, height):
    a = math.radians(yaw_deg)
    cam.location = target + Vector((math.sin(a) * dist, -math.cos(a) * dist, height))
    d = target - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()

lantern_prev = bpy.data.objects.get("lantern_preview")
for slot, frame in (("Idle", 1), ("IdleLantern", 1), ("Walk", 10), ("CrouchWalkLantern", 15)):
    act = bpy.data.actions["coop_partner_" + slot]
    ad = arm.animation_data or arm.animation_data_create()
    ad.action = act
    if act.slots: ad.action_slot = act.slots[0]
    scene.frame_set(frame)
    if lantern_prev: lantern_prev.hide_render = "Lantern" not in slot
    for view, yaw in (("back", 0), ("side", 90), ("front", 180)):
        look(cam, Vector((0, 0, 0.9)), 3.6, yaw, 0.1)
        scene.render.filepath = os.path.join(OUTDIR, f"{slot}_{view}.png")
        bpy.ops.render.render(write_still=True)
print("RENDER_DONE", OUTDIR)
