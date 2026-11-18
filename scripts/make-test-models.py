# Rebuilds the test models in source/samples/space/assets/models with Blender, headless, no window:
#
#   blender -b --factory-startup --python scripts/make-test-models.py
#
# crate.glb is a half-metre cargo crate: one mesh node under an empty (a hierarchy, no skin) with
# two materials, the sides' painted with a base-colour texture made here, the frame's a plain
# colour. arm.glb is a cargo arm 1.2 m tall: a lathed column skinned to three bones, with a two-second
# clip, Swing, that turns the base and bends the two upper joints and back. Both sit on the ground at
# the origin, metres, +Y up once exported. Nothing is random: the same script makes the same files.
import math
import os
import bpy

OUT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', 'source', 'samples', 'space', 'assets', 'models')
FPS = 24


def clear():
	bpy.ops.wm.read_factory_settings(use_empty=True)
	bpy.context.scene.render.fps = FPS


def material(name, width, height, paint):
	# The renderer takes a model's colour from its base-colour texture alone, so every material has
	# one: paint(u, v) is its colour at a texel, with a faint fixed grain over it.
	pixels = []
	for y in range(height):
		for x in range(width):
			grain = ((x * 7 + y * 13) % 17) / 17 * 0.04
			pixels += [min(1.0, k + grain) for k in paint(x / width, y / height)] + [1.0]
	image = bpy.data.images.new(name, width, height)
	image.pixels = pixels
	image.file_format = 'PNG'
	image.pack()
	mat = bpy.data.materials.new(name)
	bsdf = mat.node_tree.nodes['Principled BSDF']
	bsdf.inputs['Roughness'].default_value = 0.7
	node = mat.node_tree.nodes.new('ShaderNodeTexImage')
	node.image = image
	mat.node_tree.links.new(node.outputs['Color'], bsdf.inputs['Base Color'])
	return mat


def crate_sides(u, v):
	# Weathered green plating: a darker border, a diagonal brace and a band of hazard stripes.
	if 0.42 < v < 0.58 and 0.2 < u < 0.8:
		return [0.85, 0.62, 0.08] if int((u + v) * 12) % 2 == 0 else [0.05, 0.05, 0.05]
	if min(u, v, 1 - u, 1 - v) < 0.08 or abs(u - v) < 0.05:
		return [0.10, 0.19, 0.13]
	return [0.16, 0.30, 0.20]


def arm_paint(u, v):
	# Along the column, base (v = 0) to tip: a dark flange and knuckles on orange paint.
	height = v * 1.22
	if height < 0.06 or min(abs(height - 0.4), abs(height - 0.8)) < 0.03 or height > 1.19:
		return [0.12, 0.12, 0.13]
	return [0.80, 0.42, 0.06]


def export(path, **options):
	bpy.ops.export_scene.gltf(
	    filepath=os.path.join(OUT, path), export_format='GLB', export_yup=True, export_apply=True,
	    export_cameras=False, export_lights=False, **options)


def make_crate():
	clear()
	root = bpy.data.objects.new('Crate', None)
	bpy.context.scene.collection.objects.link(root)

	# The sides: a cube whose every face shows the whole texture.
	bpy.ops.mesh.primitive_cube_add(size=0.46, location=(0, 0, 0.25))
	body = bpy.context.object
	body.name = 'Body'
	bpy.ops.object.mode_set(mode='EDIT')
	bpy.ops.mesh.select_all(action='SELECT')
	bpy.ops.uv.reset()
	bpy.ops.object.mode_set(mode='OBJECT')
	body.data.materials.append(material('crate-sides', 128, 128, crate_sides))
	body.data.materials.append(material('crate-frame', 16, 16, lambda u, v: [0.55, 0.53, 0.50]))

	# The frame: twelve square bars along the edges, joined into the body as its second material.
	bars = []
	for axis in range(3):
		for a in (-1, 1):
			for b in (-1, 1):
				location = [0.0, 0.0, 0.0]
				others = [i for i in range(3) if i != axis]
				location[others[0]] = a * 0.235
				location[others[1]] = b * 0.235
				location[2] += 0.25
				scale = [0.02, 0.02, 0.02]
				scale[axis] = 0.25
				bpy.ops.mesh.primitive_cube_add(size=2, location=location, scale=scale)
				bars.append(bpy.context.object)
	for bar in bars:
		bar.data.materials.append(body.data.materials[1])
	bpy.ops.object.select_all(action='DESELECT')
	for bar in bars:
		bar.select_set(True)
	body.select_set(True)
	bpy.context.view_layer.objects.active = body
	bpy.ops.object.join()
	for polygon in body.data.polygons:
		polygon.material_index = 0 if polygon.area > 0.05 else 1
	body.parent = root
	export('crate.glb', export_animations=False)


def make_arm():
	clear()
	# The column's profile, (height, radius): a flange, the shaft with a knuckle at each joint, a cap.
	profile = [(0.0, 0.18), (0.05, 0.18), (0.06, 0.07)]
	for z in range(1, 23):
		height = 0.06 + z * 0.05
		knuckle = min(abs(height - 0.4), abs(height - 0.8))
		profile.append((height, 0.085 if knuckle < 0.03 else 0.06))
	profile += [(1.2, 0.05), (1.22, 0.0)]
	sides = 16
	vertices, faces = [], []
	for height, radius in profile:
		for s in range(sides):
			angle = 2 * math.pi * s / sides
			vertices.append((radius * math.cos(angle), radius * math.sin(angle), height))
	for ring in range(len(profile) - 1):
		for s in range(sides):
			a, b = ring * sides + s, ring * sides + (s + 1) % sides
			faces.append((a, b, b + sides, a + sides))
	faces.append(tuple(reversed(range(sides))))
	mesh = bpy.data.meshes.new('Arm')
	mesh.from_pydata(vertices, [], faces)
	# The paint varies along the column alone, so every vertex takes the middle of the texture across.
	uvs = mesh.uv_layers.new()
	for loop in mesh.loops:
		uvs.data[loop.index].uv = (0.5, vertices[loop.vertex_index][2] / 1.22)
	mesh.materials.append(material('arm-paint', 32, 128, arm_paint))
	arm = bpy.data.objects.new('Arm', mesh)
	bpy.context.scene.collection.objects.link(arm)

	armature = bpy.data.armatures.new('Rig')
	rig = bpy.data.objects.new('Rig', armature)
	bpy.context.scene.collection.objects.link(rig)
	bpy.context.view_layer.objects.active = rig
	bpy.ops.object.mode_set(mode='EDIT')
	names, parent = ['base', 'lower', 'upper'], None
	for index, name in enumerate(names):
		bone = armature.edit_bones.new(name)
		bone.head = (0, 0, index * 0.4)
		bone.tail = (0, 0, index * 0.4 + 0.4)
		bone.parent = parent
		bone.use_connect = parent is not None
		parent = bone
	bpy.ops.object.mode_set(mode='OBJECT')

	# Each vertex follows the bone its height is in, blended across 10 cm about each joint.
	groups = [arm.vertex_groups.new(name=name) for name in names]
	for vertex in mesh.vertices:
		height = vertex.co.z
		for index, group in enumerate(groups):
			low = 1.0 if index == 0 else min(1.0, max(0.0, (height - (index * 0.4 - 0.05)) / 0.1))
			high = 1.0 if index == 2 else min(1.0, max(0.0, ((index + 1) * 0.4 + 0.05 - height) / 0.1))
			weight = min(low, high)
			if weight > 0.0:
				group.add([vertex.index], weight, 'REPLACE')
	arm.parent = rig
	arm.modifiers.new('Rig', 'ARMATURE').object = rig

	bpy.context.view_layer.objects.active = rig
	bpy.ops.object.mode_set(mode='POSE')
	rig.animation_data_create()
	rig.animation_data.action = bpy.data.actions.new('Swing')
	# (bend, turn) at the clip's extremes: bones lie along their own +Y, so a joint bends about X
	# and the base turns about Y, carrying the bend round with it.
	keys = {'base': (0.0, 0.6), 'lower': (0.5, 0.0), 'upper': (-0.9, 0.0)}
	for frame, sign in ((0, 0.0), (FPS // 2, 1.0), (FPS, 0.0), (FPS * 3 // 2, -1.0), (FPS * 2, 0.0)):
		for name, (bend, turn) in keys.items():
			bone = rig.pose.bones[name]
			bone.rotation_mode = 'XYZ'
			bone.rotation_euler = (bend * sign, turn * sign, 0.0)
			bone.keyframe_insert('rotation_euler', frame=frame)
	bpy.ops.object.mode_set(mode='OBJECT')
	bpy.context.scene.frame_start, bpy.context.scene.frame_end = 0, FPS * 2
	export('arm.glb', export_animations=True, export_animation_mode='ACTIONS', export_skins=True)


make_crate()
make_arm()
