// This file is distributed under a BSD license. See LICENSE.txt for details.

#include "kdoc.hpp"
#include "genbitmap.hpp"
#include "genmaterial.hpp"
#include "genmesh.hpp"
#include "genoverlay.hpp"
#include "genscene.hpp"
#include "geneffect.hpp"
#include "kkriegergame.hpp"

 
KHandler KHandlers[] =
{
  // misc
  0x01, MW(Init_Misc_Load),         MW(Exec_Misc_Load),             // load
  0x02, MW(Init_Misc_Nop),          MW(Exec_Misc_Nop),              // store
  0x03, MW(Init_Misc_Nop),          MW(Exec_Misc_Nop),              // nop
  0x05, MW(Init_Misc_Nop),          MW(Exec_Misc_Time),             // time
  0x06, MW(Init_Misc_Event),        MW(Exec_Misc_Event),            // event
  0x07, MW(Init_Misc_Trigger),      MW(Exec_Misc_Trigger),          // trigger
  0x08, MW(Init_Misc_Nop),          MW(Exec_Misc_Delay),            // delay
  0x09, MW(Init_Misc_Nop),          MW(Exec_Misc_If),               // if
  0x0a, MW(Init_Misc_Nop),          MW(Exec_Misc_SetIf),            // setif
  0x0b, MW(Init_Misc_Nop),          MW(Exec_Misc_State),            // state
  0x0c, MW(Init_Misc_Nop),          MW(Exec_Misc_PlaySample),       // play sample
  0x0d, MW(Init_Misc_Demo),         MW(Exec_Misc_Demo),             // demo 
  0x0e, MW(Init_Misc_Nop),          MW(Exec_Misc_Menu),             // menu

  // kkrieger
  0x10, MW(Init_KKrieger_Para),     MW(Exec_KKrieger_Para),         // KKriegerPara
  0x11, MW(Init_KKrieger_Monster),  MW(Exec_KKrieger_Monster),      // KKriegerMonster
  0x12, MW(Init_KKrieger_Events),   MW(Exec_KKrieger_Events),       // KKriegerEvents
  0x13, MW(Init_KKrieger_Respawn),  MW(Exec_KKrieger_Respawn),
  0x14, MW(Init_KKrieger_SplashDamage),  MW(Exec_KKrieger_SplashDamage),
  0x15, MW(Init_KKrieger_MonsterFlags),  MW(Exec_KKrieger_MonsterFlags),

  // bitmap
  0x21, MW(Bitmap_Flat),            MW(Exec_Misc_Nop),              // flat
  0x22, MW(Bitmap_Perlin),          MW(Exec_Misc_Nop),              // perlin
  0x23, MW(Bitmap_Color),           MW(Exec_Misc_Nop),              // color
  0x24, MW(Bitmap_Merge),           MW(Exec_Misc_Nop),              // merge
  0x25, MW(Bitmap_Format),          MW(Exec_Misc_Nop),              // format
  0x26, MW(Bitmap_RenderTarget),    MW(Exec_Misc_Nop),              // render target
  0x27, MW(Bitmap_GlowRect),        MW(Exec_Misc_Nop),              // glowrect (old)
  0x28, MW(Bitmap_Dots),            MW(Exec_Misc_Nop),              // dots
  0x29, MW(Bitmap_Blur),            MW(Exec_Misc_Nop),              // blur
  0x2a, MW(Bitmap_Mask),            MW(Exec_Misc_Nop),              // mask
  0x2b, MW(Bitmap_HSCB),            MW(Exec_Misc_Nop),              // hscb
  0x2c, MW(Bitmap_Rotate),          MW(Exec_Misc_Nop),              // rotate
  0x2d, MW(Bitmap_Distort),         MW(Exec_Misc_Nop),              // distort
  0x2e, MW(Bitmap_Normals),         MW(Exec_Misc_Nop),              // normals
  0x2f, MW(Bitmap_Light),           MW(Exec_Misc_Nop),              // light
  0x30, MW(Bitmap_Bump),            MW(Exec_Misc_Nop),              // bump
  0x31, MW(Bitmap_Text),            MW(Exec_Misc_Nop),              // text
  0x32, MW(Bitmap_Cell),            MW(Exec_Misc_Nop),              // cell
  0x34, MW(Bitmap_Gradient),        MW(Exec_Misc_Nop),              // gradient
  0x35, MW(Bitmap_Range),           MW(Exec_Misc_Nop),              // range
  0x36, MW(Bitmap_RotateMul),       MW(Exec_Misc_Nop),              // rotatemul
  0x37, MW(Bitmap_Twirl),           MW(Exec_Misc_Nop),              // twirl
  0x38, MW(Bitmap_Sharpen),         MW(Exec_Misc_Nop),              // sharpen
  0x39, MW(Bitmap_GlowRect),        MW(Exec_Misc_Nop),              // glowrect
  0x3b, MW(Bitmap_ColorBalance),    MW(Exec_Misc_Nop),              // color balance

  // effecte
//  0x60, MW(Init_Effect_Dummy),      MW(Exec_Effect_Dummy),          // dummy-effect
  0x61, MW(Init_Effect_Print),      MW(Exec_Effect_Print),          // print text/menu
  0x63, MW(Init_Effect_PartEmitter),MW(Exec_Effect_PartEmitter),    // particles
  0x64, MW(Init_Effect_PartSystem), MW(Exec_Effect_PartSystem),     // particles

  // mesh
  0x81, MW(Mesh_Cube),              MW(Exec_Misc_Nop),              // cube
  0x82, MW(Mesh_Cylinder),          MW(Exec_Misc_Nop),              // cylinder
//  0x83, MW(Mesh_Torus),             MW(Exec_Misc_Nop),              // torus
//  0x84, MW(Mesh_Sphere),            MW(Exec_Misc_Nop),              // sphere
//  0x85, MW(Mesh_SelectAll),         MW(Exec_Misc_Nop),              // select all
  0x86, MW(Mesh_SelectCube),        MW(Exec_Misc_Nop),              // select cube
  0x87, MW(Mesh_Subdivide),         MW(Exec_Misc_Nop),              // subdivide
  0x88, MW(Mesh_Transform),         MW(Exec_Misc_Nop),              // transform
  0x89, MW(Mesh_TransformEx),       MW(Exec_Misc_Nop),              // transformex
  0x8a, MW(Mesh_Crease),            MW(Exec_Misc_Nop),              // crease
//  0x8b, MW(Mesh_UnCrease),          MW(Exec_Misc_Nop),              // uncrease
  0x8c, MW(Mesh_Triangulate),       MW(Exec_Misc_Nop),              // triangulate
//  0x8d, MW(Mesh_Cut),               MW(Exec_Misc_Nop),              // cut
//  0x8e, MW(Mesh_ExtrudeNormal),     MW(Exec_Misc_Nop),              // extrudenormal
  0x8f, MW(Mesh_Displace),          MW(Exec_Misc_Nop),              // displace
// bevel ONLY used in full level!
  0x90, MW(Mesh_Bevel),             MW(Exec_Misc_Nop),              // bevel
  0x91, MW(Mesh_Perlin),            MW(Exec_Misc_Nop),              // perlin
  0x92, MW(Mesh_Add),               MW(Exec_Misc_Nop),              // add
// deletefaces ONLY used in full level! (and kkrieger-logo)
  0x93, MW(Mesh_DeleteFaces),       MW(Exec_Misc_Nop),              // delete faces
//  0x94, MW(Mesh_SelectRandom),      MW(Exec_Misc_Nop),              // select random
  0x95, MW(Mesh_Multiply),          MW(Exec_Misc_Nop),              // multiply
  0x96, MW(Mesh_MatLink),           MW(Exec_Misc_Nop),              // matlink
//  0x99, MW(Mesh_BeginRecord),       MW(Exec_Misc_Nop),              // beginrec
  0x9a, MW(Mesh_Extrude),           MW(Exec_Misc_Nop),              // extrude
  0x9c, MW(Mesh_CollisionCube),     MW(Exec_Misc_Nop),              // collcube
// grid ONLY used in full level! (and kkrieger-logo)
  0x9d, MW(Mesh_Grid),              MW(Exec_Misc_Nop),              // grid
  0xa0, MW(Mesh_Bend),              MW(Exec_Misc_Nop),              // bend
//  0xa1, MW(Mesh_SelectLogic),       MW(Exec_Misc_Nop),              // select logic
//  0xa2, MW(Mesh_SelectGrow),        MW(Exec_Misc_Nop),              // select grow
//  0xa3, MW(Mesh_Invert),            MW(Exec_Misc_Nop),              // invert
//  0xa4, MW(Mesh_SetPivot),          MW(Exec_Misc_Nop),              // set pivot
  0xa5, MW(Mesh_UVProjection),      MW(Exec_Misc_Nop),              // uvproject
  0xa6, MW(Mesh_Center),            MW(Exec_Misc_Nop),              // center
  0xa7, MW(Mesh_AutoCollision),     MW(Exec_Misc_Nop),              // autocoll
  0xa8, MW(Mesh_SelectSphere),      MW(Exec_Misc_Nop),              // selectsphere
//  0xa9, MW(Mesh_SelectFace),        MW(Exec_Misc_Nop),              // selectface
//  0xaa, MW(Mesh_SelectAngle),       MW(Exec_Misc_Nop),              // selectangle
  0xab, MW(Mesh_Bend2),             MW(Exec_Misc_Nop),              // bend 2
//  0xac, MW(Mesh_SmoothAngle),       MW(Exec_Misc_Nop),              // smangle
  0xad, MW(Mesh_Color),             MW(Exec_Misc_Nop),              // color
//  0xae, MW(Mesh_BendS),             MW(Exec_Misc_Nop),              // bend spline
  0xaf, MW(Mesh_LightSlot),         MW(Exec_Misc_Nop),              // light slot
  0xb0, MW(Mesh_ShadowEnable),      MW(Exec_Misc_Nop),              // shadowenable
  0xb3, MW(Mesh_SingleVert),        MW(Exec_Misc_Nop),              // single vertex

  // scene
  0xc0, MW(Init_Scene_Scene),       MW(Exec_Scene_Scene),           // scene
  0xc1, MW(Init_Scene_Add),         MW(Exec_Scene_Add),             // add
  0xc2, MW(Init_Scene_Multiply),    MW(Exec_Scene_Multiply),        // multiply
  0xc3, MW(Init_Scene_Transform),   MW(Exec_Scene_Transform),       // transform
  0xc4, MW(Init_Scene_Light),       MW(Exec_Scene_Light),           // light
  0xc5, MW(Init_Scene_Particles),   MW(Exec_Scene_Particles),       // scene multply particles
  0xc6, MW(Init_Scene_Camera),      MW(Exec_Scene_Camera),          // camera

  0xc7, MW(Init_Scene_Limb),        MW(Exec_Scene_Limb),            // limb
  0xc8, MW(Init_Scene_Walk),        MW(Exec_Scene_Walk),            // walk
  0xc9, MW(Init_Scene_Rotate),      MW(Exec_Scene_Rotate),          // rotate
//  0xca, MW(Init_Scene_Forward),     MW(Exec_Scene_Forward),         // forward
  0xcb, MW(Init_Scene_Sector),      MW(Exec_Scene_Sector),          // sector
  0xcd, MW(Init_Scene_Portal),      MW(Exec_Scene_Portal),          // portal
  0xce, MW(Init_Scene_Physic),      MW(Exec_Scene_Physic),          // physic
  0xce, MW(Init_Scene_Physic),      MW(Exec_Scene_Physic),          // physic

  // material
  0xd0, MW(Init_Material_Material), MW(Exec_Material_Material),     // material
  0xd1, MW(Material_Add),           MW(Exec_Misc_Nop),              // add material
  0xd2, MW(Init_Scene_MatHack),     MW(Exec_Scene_MatHack),         // link material for animation

  // ipp
  0xe2, MW(Init_IPP_Copy),          MW(Exec_IPP_Copy),              // copy
  0xe3, MW(Init_IPP_Blur),          MW(Exec_IPP_Blur),              // blur
//  0xe4, MW(Init_IPP_Crashzoom),     MW(Exec_IPP_Crashzoom),         // crashzoom
//  0xe5, MW(Init_IPP_Sharpen),       MW(Exec_IPP_Sharpen),           // sharpen
  0xe6, MW(Init_IPP_Color),         MW(Exec_IPP_Color),             // color
  0xe7, MW(Init_IPP_Merge),         MW(Exec_IPP_Merge),             // merge
  0xe8, MW(Init_IPP_Mask),          MW(Exec_IPP_Mask),              // mask
//  0xe9, MW(Init_IPP_Layer2D),       MW(Exec_IPP_Layer2D),           // layer2d
  0xea, MW(Init_IPP_Select),        MW(Exec_IPP_Select),            // select
  0xeb, MW(Init_IPP_RenderTarget),  MW(Exec_IPP_RenderTarget),      // render target
  0xf0, MW(Init_IPP_Viewport),      MW(Exec_IPP_Viewport),          // viewport

  // minmesh
//  0x100, MW(MinMesh_SingleVert),    MW(Exec_Misc_Nop),              // single vert
//  0x101, MW(MinMesh_Grid),          MW(Exec_Misc_Nop),              // grid
//  0x102, MW(MinMesh_Cube),          MW(Exec_Misc_Nop),              // cube
//  0x103, MW(MinMesh_Torus),         MW(Exec_Misc_Nop),              // torus
//  0x104, MW(MinMesh_Sphere),        MW(Exec_Misc_Nop),              // sphere
//  0x105, MW(MinMesh_Cylinder),      MW(Exec_Misc_Nop),              // cylinder
//  0x106, MW(MinMesh_XSI),           MW(Exec_Misc_Nop),              // xsi

//  0x110, MW(MinMesh_MatLink),       MW(Exec_Misc_Nop),              // matlink
//  0x111, MW(MinMesh_Add),           MW(Exec_Misc_Nop),              // add
//  0x112, MW(MinMesh_SelectAll),     MW(Exec_Misc_Nop),              // select all
//  0x113, MW(MinMesh_SelectCube),    MW(Exec_Misc_Nop),              // select cube
//  0x114, MW(MinMesh_DeleteFaces),   MW(Exec_Misc_Nop),              // delete faces
//  0x115, MW(MinMesh_Invert),        MW(Exec_Misc_Nop),              // invert

//  0x120, MW(MinMesh_TransformEx),   MW(Exec_Misc_Nop),              // transform
//  0x121, MW(MinMesh_TransformEx),   MW(Exec_Misc_Nop),              // transform ex
//  0x122, MW(MinMesh_ExtrudeNormal), MW(Exec_Misc_Nop),              // extrude normal
//  0x123, MW(MinMesh_Displace),      MW(Exec_Misc_Nop),              // displace
//  0x124, MW(MinMesh_Perlin),        MW(Exec_Misc_Nop),              // perlin
//  0x125, MW(MinMesh_Bend2),         MW(Exec_Misc_Nop),              // bend 2

  // end!
  0,0,0,
};
