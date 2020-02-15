/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 by Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Multiplexed Reality
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/vr/intern/vr_widget_texturepaint.cpp
 *   \ingroup vr
 *
 */

#include <float.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "vr_types.h"
#include <list>
#include <assert.h>

#include "vr_main.h"
#include "vr_ui.h"

#include "vr_widget_texturepaint.h"
#include "vr_widget_transform.h"
#include "vr_widget_switchcomponent.h"

#include "vr_draw.h"
#include "vr_math.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_dial_2d.h"
#include "BLI_gsqueue.h"
#include "BLI_ghash.h"
#include "BLI_hash.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_brush_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BKE_brush.h"
#include "BKE_ccg.h"
#include "BKE_colortools.h"
#include "BKE_colorband.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_key.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_modifier.h"
#include "BKE_multires.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_particle.h"
#include "BKE_pbvh.h"
#include "BKE_pointcache.h"
#include "BKE_report.h"
#include "BKE_scene.h"
//#include "BKE_screen.h"
struct ScrArea *BKE_screen_find_area_xy(struct bScreen *sc, const int spacetype, int x, int y);
struct ARegion *BKE_area_find_region_xy(struct ScrArea *sa, const int regiontype, int x, int y);
#include "BKE_subdiv_ccg.h"
#include "BKE_subsurf.h"
#include "BKE_undo_system.h"

#include "UI_interface.h"
#include "UI_view2d.h"
#include "UI_resources.h"

#include "bmesh.h"
#include "bmesh_tools.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ED_image.h"
#include "ED_object.h"
#include "ED_paint.h"
#include "ED_screen.h"
#include "ED_view3d.h"

#include "GPU_draw.h"
#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_state.h"

#include "MEM_guardedalloc.h"

#include "paint_intern.h"
//#include "sculpt_intern.h"

#include "WM_api.h"
#include "WM_types.h"
#include "WM_message.h"
#include "wm_message_bus.h"
#include "WM_toolsystem.h"
#include "WM_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "IMB_colormanagement.h"

/***************************************************************************************************
 * \class                               Widget_TexturePaint
 ***************************************************************************************************
 * Interaction widget for the TexturePaint tool.
 *
 **************************************************************************************************/
#define WIDGET_TEXTUREPAINT_MAX_RADIUS 500.0f /* Max paint radius (in Blender meters) */

Widget_TexturePaint Widget_TexturePaint::obj;

float Widget_TexturePaint::paint_radius(100.0f);
float Widget_TexturePaint::paint_strength(1.0f);

Coord3Df Widget_TexturePaint::p_hmd;
Coord3Df Widget_TexturePaint::p_cursor;
float Widget_TexturePaint::dist;
float Widget_TexturePaint::paint_radius_prev;
float Widget_TexturePaint::paint_strength_prev;
bool Widget_TexturePaint::param_mode(false);
bool Widget_TexturePaint::stroke_started(false);
bool Widget_TexturePaint::is_dragging(false);
bool Widget_TexturePaint::stroke_canceled(false);

VR_Side Widget_TexturePaint::cursor_side;

int Widget_TexturePaint::mode(BRUSH_STROKE_NORMAL);
int Widget_TexturePaint::mode_orig(BRUSH_STROKE_NORMAL);
int Widget_TexturePaint::brush(PAINT_TOOL_DRAW);
float Widget_TexturePaint::location[3];
float Widget_TexturePaint::mouse[2];
float Widget_TexturePaint::pressure(1.0f);
bool Widget_TexturePaint::use_trigger_pressure(true);
bool Widget_TexturePaint::raycast(true);
bool Widget_TexturePaint::dyntopo(false);
char Widget_TexturePaint::symmetry(0x00);
bool Widget_TexturePaint::pen_flip(false);
bool Widget_TexturePaint::ignore_background_click(true);

float BRUSH_SCALE = 1.0 / 100.0 * 0.05;  // this is wrong, TODO: fixme

/* Dummy op for paint functions. */
static wmOperator paint_dummy_op;
/* Dummy event for paint functions. */
static wmEvent paint_dummy_event;

// COPIED from paint_image.c

/**
 * This is a static resource for non-global access.
 * Maybe it should be exposed as part of the paint operation,
 * but for now just give a public interface.
 */
static ImagePaintPartialRedraw imapaintpartial = {0, 0, 0, 0, 0};

/************************ image paint poll ************************/

static Brush *image_paint_brush(bContext *C)
{
  Scene *scene = CTX_data_scene(C);
  ToolSettings *settings = scene->toolsettings;

  return BKE_paint_brush(&settings->imapaint.paint);
}

static bool image_paint_poll_ex(bContext *C, bool check_tool)
{
  Object *obact;

  if (!image_paint_brush(C)) {
    return 0;
  }

  obact = CTX_data_active_object(C);
  if ((obact && obact->mode & OB_MODE_TEXTURE_PAINT) && CTX_wm_region_view3d(C)) {
    if (!check_tool || WM_toolsystem_active_tool_is_brush(C)) {
      return 1;
    }
  }
  else {
    SpaceImage *sima = CTX_wm_space_image(C);

    if (sima) {
      ARegion *ar = CTX_wm_region(C);

      if ((sima->mode == SI_MODE_PAINT) && ar->regiontype == RGN_TYPE_WINDOW) {
        return 1;
      }
    }
  }

  return 0;
}

static bool image_paint_poll(bContext *C)
{
  return image_paint_poll_ex(C, true);
}

static bool image_paint_poll_ignore_tool(bContext *C)
{
  return image_paint_poll_ex(C, false);
}

static bool image_paint_2d_clone_poll(bContext *C)
{
  Brush *brush = image_paint_brush(C);

  if (!CTX_wm_region_view3d(C) && image_paint_poll(C)) {
    if (brush && (brush->imagepaint_tool == PAINT_TOOL_CLONE)) {
      if (brush->clone.image) {
        return 1;
      }
    }
  }

  return 0;
}

/************************ paint operator ************************/
typedef enum eTexPaintMode {
  PAINT_MODE_2D,
  PAINT_MODE_3D_PROJECT,
} eTexPaintMode;

typedef struct PaintOperation {
  eTexPaintMode mode;

  void *custom_paint;

  float prevmouse[2];
  float startmouse[2];
  double starttime;

  void *cursor;
  ViewContext vc;
} PaintOperation;

static void gradient_draw_line(bContext *UNUSED(C), int x, int y, void *customdata)
{
  PaintOperation *pop = (PaintOperation *)customdata;

  if (pop) {
    GPU_line_smooth(true);
    GPU_blend(true);

    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);

    ARegion *ar = pop->vc.ar;

    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

    GPU_line_width(4.0);
    immUniformColor4ub(0, 0, 0, 255);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex2i(pos, x, y);
    immVertex2i(pos, pop->startmouse[0] + ar->winrct.xmin, pop->startmouse[1] + ar->winrct.ymin);
    immEnd();

    GPU_line_width(2.0);
    immUniformColor4ub(255, 255, 255, 255);

    immBegin(GPU_PRIM_LINES, 2);
    immVertex2i(pos, x, y);
    immVertex2i(pos, pop->startmouse[0] + ar->winrct.xmin, pop->startmouse[1] + ar->winrct.ymin);
    immEnd();

    immUnbindProgram();

    GPU_blend(false);
    GPU_line_smooth(false);
  }
}

static PaintOperation *texture_paint_init(bContext *C, wmOperator *op, const float mouse[2])
{
  printf("texture paint init\n");
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Scene *scene = CTX_data_scene(C);
  ToolSettings *settings = scene->toolsettings;
  PaintOperation *pop = (PaintOperation *)MEM_callocN(sizeof(PaintOperation),
                                                      "PaintOperation"); /* caller frees */
  Brush *brush = BKE_paint_brush(&settings->imapaint.paint);
  int mode = RNA_enum_get(op->ptr, "mode");
  ED_view3d_viewcontext_init(C, &pop->vc, depsgraph);

  copy_v2_v2(pop->prevmouse, mouse);
  copy_v2_v2(pop->startmouse, mouse);

  /* initialize from context */
  if (CTX_wm_region_view3d(C)) {
    ViewLayer *view_layer = CTX_data_view_layer(C);
    Object *ob = OBACT(view_layer);
    bool uvs, mat, tex, stencil;
    if (!BKE_paint_proj_mesh_data_check(scene, ob, &uvs, &mat, &tex, &stencil)) {
      BKE_paint_data_warning(op->reports, uvs, mat, tex, stencil);
      MEM_freeN(pop);
      WM_event_add_notifier(C, NC_SCENE | ND_TOOLSETTINGS, NULL);
      return NULL;
    }
    pop->mode = PAINT_MODE_3D_PROJECT;
    pop->custom_paint = paint_proj_new_stroke(C, ob, mouse, mode);
  }
  else {
    pop->mode = PAINT_MODE_2D;
    pop->custom_paint = paint_2d_new_stroke(C, op, mode);
  }

  if (!pop->custom_paint) {
    MEM_freeN(pop);
    return NULL;
  }

  if ((brush->imagepaint_tool == PAINT_TOOL_FILL) && (brush->flag & BRUSH_USE_GRADIENT)) {
    pop->cursor = WM_paint_cursor_activate(CTX_wm_manager(C),
                                           SPACE_TYPE_ANY,
                                           RGN_TYPE_ANY,
                                           image_paint_poll,
                                           gradient_draw_line,
                                           pop);
  }

  settings->imapaint.flag |= IMAGEPAINT_DRAWING;

  // let's assert things are good here
  UndoStack *ustack = CTX_wm_manager(C)->undo_stack;

  // cheap hack?
  if (!(ustack->step_init == NULL)) {
    printf("Error! ustack -> step init NOT NULL!\n");
    ED_image_undo_push_end();
  }

  BLI_assert(ustack->step_init == NULL);

  ED_image_undo_push_begin(op->type->name, PAINT_MODE_TEXTURE_2D);

  BLI_assert(!(pop == NULL));

  return pop;
}

static void paint_stroke_update_step(bContext *C, struct PaintStroke *stroke, PointerRNA *itemptr)
{

  PaintOperation *pop = (PaintOperation *)paint_stroke_mode_data(stroke);
  Scene *scene = CTX_data_scene(C);
  ToolSettings *toolsettings = CTX_data_tool_settings(C);
  UnifiedPaintSettings *ups = &toolsettings->unified_paint_settings;
  Brush *brush = BKE_paint_brush(&toolsettings->imapaint.paint);

  float alphafac = (brush->flag & BRUSH_ACCUMULATE) ? ups->overlap_factor : 1.0f;

  /* initial brush values. Maybe it should be considered moving these to stroke system */
  float startalpha = BKE_brush_alpha_get(scene, brush);

  float mouse[2];
  float pressure;
  float size;
  float distance = paint_stroke_distance_get(stroke);
  int eraser;

  RNA_float_get_array(itemptr, "mouse", mouse);
  pressure = RNA_float_get(itemptr, "pressure");
  eraser = RNA_boolean_get(itemptr, "pen_flip");
  size = RNA_float_get(itemptr, "size");


  //printf("update m:%f %f, p: %f, s: %f, f: %d\n", mouse[0], mouse[1], pressure, size, eraser);

  /* stroking with fill tool only acts on stroke end */
  if (brush->imagepaint_tool == PAINT_TOOL_FILL) {
    copy_v2_v2(pop->prevmouse, mouse);
    return;
  }

  if (BKE_brush_use_alpha_pressure(scene, brush)) {
    BKE_brush_alpha_set(scene, brush, max_ff(0.0f, startalpha * pressure * alphafac));
  }
  else {
    BKE_brush_alpha_set(scene, brush, max_ff(0.0f, startalpha * alphafac));
  }

  if ((brush->flag & BRUSH_DRAG_DOT) || (brush->flag & BRUSH_ANCHORED)) {
    UndoStack *ustack = CTX_wm_manager(C)->undo_stack;
    ED_image_undo_restore(ustack->step_init);
  }

  if (pop->mode == PAINT_MODE_3D_PROJECT) {
    paint_proj_stroke(
        C, pop->custom_paint, pop->prevmouse, mouse, eraser, pressure, distance, size);
  }
  else {
    paint_2d_stroke(pop->custom_paint, pop->prevmouse, mouse, eraser, pressure, distance, size);
  }

  copy_v2_v2(pop->prevmouse, mouse);

  /* restore brush values */
  BKE_brush_alpha_set(scene, brush, startalpha);
}

static void paint_stroke_redraw(const bContext *C, struct PaintStroke *stroke, bool final)
{
  PaintOperation *pop = (PaintOperation *)paint_stroke_mode_data(stroke);

  if (pop->mode == PAINT_MODE_3D_PROJECT) {
    paint_proj_redraw(C, pop->custom_paint, final);
  }
  else {
    paint_2d_redraw(C, pop->custom_paint, final);
  }
}

static void paint_stroke_done(const bContext *C, struct PaintStroke *stroke)
{
  printf("paint stroke done\n");
  Scene *scene = CTX_data_scene(C);
  ToolSettings *toolsettings = scene->toolsettings;

  if (stroke == NULL) {
    printf("error, stroke NULL!\n");
    return;
  }
  BLI_assert(stroke != NULL);

  PaintOperation *pop = (PaintOperation *)paint_stroke_mode_data(stroke);
  Brush *brush = BKE_paint_brush(&toolsettings->imapaint.paint);

  toolsettings->imapaint.flag &= ~IMAGEPAINT_DRAWING;

  if (brush->imagepaint_tool == PAINT_TOOL_FILL) {
    if (brush->flag & BRUSH_USE_GRADIENT) {
      if (pop->mode == PAINT_MODE_2D) {
        paint_2d_gradient_fill(C, brush, pop->startmouse, pop->prevmouse, pop->custom_paint);
      }
      else {
        paint_proj_stroke(C,
                          pop->custom_paint,
                          pop->startmouse,
                          pop->prevmouse,
                          paint_stroke_flipped(stroke),
                          1.0,
                          0.0,
                          BKE_brush_size_get(scene, brush));
        /* two redraws, one for GPU update, one for notification */
        paint_proj_redraw(C, pop->custom_paint, false);
        paint_proj_redraw(C, pop->custom_paint, true);
      }
    }
    else {
      if (pop->mode == PAINT_MODE_2D) {
        float color[3];
        if (paint_stroke_inverted(stroke)) {
          srgb_to_linearrgb_v3_v3(color, BKE_brush_secondary_color_get(scene, brush));
        }
        else {
          srgb_to_linearrgb_v3_v3(color, BKE_brush_color_get(scene, brush));
        }
        paint_2d_bucket_fill(C, color, brush, pop->prevmouse, pop->custom_paint);
      }
      else {
        paint_proj_stroke(C,
                          pop->custom_paint,
                          pop->startmouse,
                          pop->prevmouse,
                          paint_stroke_flipped(stroke),
                          1.0,
                          0.0,
                          BKE_brush_size_get(scene, brush));
        /* two redraws, one for GPU update, one for notification */
        paint_proj_redraw(C, pop->custom_paint, false);
        paint_proj_redraw(C, pop->custom_paint, true);
      }
    }
  }
  if (pop->mode == PAINT_MODE_3D_PROJECT) {
    paint_proj_stroke_done(pop->custom_paint);
  }
  else {
    paint_2d_stroke_done(pop->custom_paint);
  }

  if (pop->cursor) {
    WM_paint_cursor_end(CTX_wm_manager(C), (wmPaintCursor *)pop->cursor);
  }

  ED_image_undo_push_end();

  /* duplicate warning, see texpaint_init */
#if 0
  if (pop->s.warnmultifile) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "Image requires 4 color channels to paint: %s",
                pop->s.warnmultifile);
  }
  if (pop->s.warnpackedfile) {
    BKE_reportf(op->reports,
                RPT_WARNING,
                "Packed MultiLayer files cannot be painted: %s",
                pop->s.warnpackedfile);
  }
#endif
  MEM_freeN(pop);
}

static bool paint_stroke_test_start(bContext *C, wmOperator *op, const float mouse[2])
{
  printf("Paint stroke test start\n");
  PaintOperation *pop;

  /*if (Widget_TexturePaint::raycast) {
    sculpt_stroke_get_location(C, Widget_TexturePaint::location, Widget_TexturePaint::mouse);
  }
  else {
    ViewContext vc;
    Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
    ED_view3d_viewcontext_init(C, &vc, depsgraph);
    Object *ob = vc.obact;
    SculptSession *ss = ob->sculpt;
    StrokeCache *cache = ss->cache;
    if (cache) {
      const Brush *brush = BKE_paint_brush(BKE_paint_get_active_from_context(C));
      sculpt_stroke_modifiers_check(C, ob, brush);

      / * Test if object mesh is within sculpt sphere radius. * /
      Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
      int totnode;
      const bool use_original = sculpt_tool_needs_original(brush->sculpt_tool) ?
                                    true :
                                    ss->cache->original;
      const float radius_scale = 1.25f;
      cache->radius = Widget_TexturePaint::sculpt_radius;
      sculpt_pbvh_gather_generic(ob, sd, brush, use_original, radius_scale, &totnode);
      if (totnode) {
        float obimat[4][4];
        invert_m4_m4(obimat, ob->obmat);
        mul_m4_v3(obimat, Widget_TexturePaint::location);
        copy_v3_v3(cache->true_location, Widget_TexturePaint::location);
      }
    }
  }*/

  /* TODO Should avoid putting this here. Instead, last position should be requested
   * from stroke system. */

  if (!(pop = texture_paint_init(C, op, mouse))) {
    printf("..failed\n");
    return false;
  }

  paint_stroke_set_mode_data((PaintStroke *)op->customdata, pop);
  printf("..succeeded\n");
  return true;
}

void raycast_mouse(bContext *C)
{

/* Get the 3d position and 2d-projected position of the VR cursor. */
  memcpy(Widget_TexturePaint::location,
         VR_UI::cursor_position_get(VR_SPACE_BLENDER, Widget_TexturePaint::cursor_side).m[3],
         sizeof(float) * 3);
  if (Widget_TexturePaint::raycast) {
    ARegion *ar = CTX_wm_region(C);
    RegionView3D *rv3d = (RegionView3D *)ar->regiondata;
    float projmat[4][4];
    mul_m4_m4m4(projmat, (float(*)[4])rv3d->winmat, (float(*)[4])rv3d->viewmat);
    mul_project_m4_v3(projmat, Widget_TexturePaint::location);
    Widget_TexturePaint::mouse[0] = (int)((ar->winx / 2.0f) +
                                          (ar->winx / 2.0f) * Widget_TexturePaint::location[0]);
    Widget_TexturePaint::mouse[1] = (int)((ar->winy / 2.0f) +
                                          (ar->winy / 2.0f) * Widget_TexturePaint::location[1]);
  }

  //printf("mouse position %f %f\n", Widget_TexturePaint::mouse[0], Widget_TexturePaint::mouse[0]);

}

static int paint_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  printf("paint_invoke\n");
  int retval;

  op->customdata = paint_stroke_new(C,
                                    op,
                                    NULL,
                                    paint_stroke_test_start,
                                    paint_stroke_update_step,
                                    paint_stroke_redraw,
                                    paint_stroke_done,
                                    event->type);


  if (op->customdata == NULL) {
    printf("Error! paint stroke is null!\n");
  }

  raycast_mouse(C);

  Widget_TexturePaint::pressure =
      vr_get_obj()->controller[Widget_TexturePaint::cursor_side]->trigger_pressure;

  /*if ((retval = op->type->modal(C, op, event)) == OPERATOR_FINISHED) {
    paint_stroke_free(C, op);
    return OPERATOR_FINISHED;
  }*/
  /* add modal handler */
  // WM_event_add_modal_handler(C, op);

  // OPERATOR_RETVAL_CHECK(retval);
  // BLI_assert(retval == OPERATOR_RUNNING_MODAL);

  bool test_start = paint_stroke_test_start(C, op, Widget_TexturePaint::mouse);
  if (!test_start) {
    return OPERATOR_CANCELLED;
  }

  /*if (Widget_TexturePaint::raycast) {
    sculpt_stroke_get_location(C, Widget_TexturePaint::location, Widget_TexturePaint::mouse);
  }
  else {
    ViewContext vc;
    Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
    ED_view3d_viewcontext_init(C, &vc, depsgraph);
    Object *ob = vc.obact;
    SculptSession *ss = ob->sculpt;
    StrokeCache *cache = ss->cache;
    if (cache) {
      const Brush *brush = BKE_paint_brush(BKE_paint_get_active_from_context(C));
      sculpt_stroke_modifiers_check(C, ob, brush);

      /* Test if object mesh is within sculpt sphere radius. * /
      Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
      int totnode;
      const bool use_original = sculpt_tool_needs_original(brush->sculpt_tool) ?
                                    true :
                                    ss->cache->original;
      const float radius_scale = 1.25f;
      cache->radius = Widget_TexturePaint::sculpt_radius;
      sculpt_pbvh_gather_generic(ob, sd, brush, use_original, radius_scale, &totnode);
      if (totnode) {
        float obimat[4][4];
        invert_m4_m4(obimat, ob->obmat);
        mul_m4_v3(obimat, Widget_TexturePaint::location);
        copy_v3_v3(cache->true_location, Widget_TexturePaint::location);
      }
    }
  }*/

  // SculptSession *ss = CTX_data_active_object(C)->sculpt;
  // memcpy(ss->cache->true_location, Widget_TexturePaint::location, sizeof(float) * 3);

  //redundant!
  //texture_paint_init(C, op, Widget_TexturePaint::mouse);

  return OPERATOR_RUNNING_MODAL;
}

static int paint_exec(bContext *C, wmOperator *op)
{
  /*PropertyRNA *strokeprop;
  PointerRNA firstpoint;
  float mouse[2];

  strokeprop = RNA_struct_find_property(op->ptr, "stroke");

  if (!RNA_property_collection_lookup_int(op->ptr, strokeprop, 0, &firstpoint)) {
    return OPERATOR_CANCELLED;
  }

  RNA_float_get_array(&firstpoint, "mouse", mouse);

  op->customdata = paint_stroke_new(C,
                                    op,
                                    NULL,
                                    paint_stroke_test_start,
                                    paint_stroke_update_step,
                                    paint_stroke_redraw,
                                    paint_stroke_done,
                                    0);
  /* frees op->customdata * /
  //return paint_stroke_exec(C, op);*/


    //update input stuff
  Widget_TexturePaint::pressure =
      vr_get_obj()->controller[Widget_TexturePaint::cursor_side]->trigger_pressure;
    raycast_mouse(C);


  //itemptr stores some data that paint_stroke_update_step needs
  /*RNA_float_get_array(itemptr, "mouse", mouse);
  pressure = RNA_float_get(itemptr, "pressure");
  eraser = RNA_boolean_get(itemptr, "pen_flip");
  size = RNA_float_get(itemptr, "size");*/
  
  PointerRNA props_ptr;
  //wmOperatorType *ot = WM_operatortype_find("PAINT_OT_image_paint", true);
  //WM_operator_properties_create_ptr(&props_ptr, ot);
  float m[2] = {Widget_TexturePaint::mouse[0], Widget_TexturePaint::mouse[1]};
  RNA_collection_add(op->ptr, "stroke", &props_ptr);  
  RNA_float_set_array(&props_ptr, "mouse", m);
  RNA_float_set(&props_ptr, "pressure", Widget_TexturePaint::pressure);
  RNA_boolean_set(&props_ptr, "pen_flip", Widget_TexturePaint::pen_flip);
  RNA_float_set(&props_ptr, "size", Widget_TexturePaint::paint_radius);
  paint_stroke_update_step(C, (PaintStroke *)op->customdata, &props_ptr);


  paint_stroke_redraw(C, (PaintStroke *)op->customdata, false);
  
  
  
  /*float m[2] = {Widget_TexturePaint::mouse[0], Widget_TexturePaint::mouse[1]};
  RNA_float_set_array(op->ptr, "mouse", m);
  RNA_float_set(op->ptr, "pressure", Widget_TexturePaint::pressure);
  RNA_boolean_set(op->ptr, "pen_flip", Widget_TexturePaint::pen_flip);
  RNA_float_set(op->ptr, "pen_flip", Widget_TexturePaint::paint_radius);
  paint_stroke_update_step(C, (PaintStroke*) op->customdata,op->ptr );*/


  
  return OPERATOR_FINISHED;
}

/************************ cursor drawing *******************************/

static void toggle_paint_cursor(bContext *C, int enable)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  Scene *scene = CTX_data_scene(C);
  ToolSettings *settings = scene->toolsettings;

  if (settings->imapaint.paintcursor && !enable) {
    WM_paint_cursor_end(wm, (wmPaintCursor *)settings->imapaint.paintcursor);
    settings->imapaint.paintcursor = NULL;
    paint_cursor_delete_textures();
  }
  else if (enable) {
    paint_cursor_start(C, image_paint_poll);
  }
}

/************************ grab clone operator ************************/

typedef struct GrabClone {
  float startoffset[2];
  int startx, starty;
} GrabClone;

static void grab_clone_apply(bContext *C, wmOperator *op)
{
  Brush *brush = image_paint_brush(C);
  float delta[2];

  RNA_float_get_array(op->ptr, "delta", delta);
  add_v2_v2(brush->clone.offset, delta);
  ED_region_tag_redraw(CTX_wm_region(C));
}

static int grab_clone_exec(bContext *C, wmOperator *op)
{
  grab_clone_apply(C, op);

  return OPERATOR_FINISHED;
}

static int grab_clone_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  Brush *brush = image_paint_brush(C);
  GrabClone *cmv;

  cmv = (GrabClone *)MEM_callocN(sizeof(GrabClone), "GrabClone");
  copy_v2_v2(cmv->startoffset, brush->clone.offset);
  cmv->startx = event->x;
  cmv->starty = event->y;
  op->customdata = cmv;

  WM_event_add_modal_handler(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int grab_clone_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  Brush *brush = image_paint_brush(C);
  ARegion *ar = CTX_wm_region(C);
  GrabClone *cmv = (GrabClone *)op->customdata;
  float startfx, startfy, fx, fy, delta[2];
  int xmin = ar->winrct.xmin, ymin = ar->winrct.ymin;

  switch (event->type) {
    case LEFTMOUSE:
    case MIDDLEMOUSE:
    case RIGHTMOUSE:  // XXX hardcoded
      MEM_freeN(op->customdata);
      return OPERATOR_FINISHED;
    case MOUSEMOVE:
      /* mouse moved, so move the clone image */
      UI_view2d_region_to_view(
          &ar->v2d, cmv->startx - xmin, cmv->starty - ymin, &startfx, &startfy);
      UI_view2d_region_to_view(&ar->v2d, event->x - xmin, event->y - ymin, &fx, &fy);

      delta[0] = fx - startfx;
      delta[1] = fy - startfy;
      RNA_float_set_array(op->ptr, "delta", delta);

      copy_v2_v2(brush->clone.offset, cmv->startoffset);

      grab_clone_apply(C, op);
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

static void grab_clone_cancel(bContext *UNUSED(C), wmOperator *op)
{
  MEM_freeN(op->customdata);
}

/******************** sample color operator ********************/
typedef struct {
  bool show_cursor;
  short event_type;
  float initcolor[3];
  bool sample_palette;
} SampleColorData;

static void sample_color_update_header(SampleColorData *data, bContext *C)
{
  char msg[UI_MAX_DRAW_STR];
  ScrArea *sa = CTX_wm_area(C);

  if (sa) {
    BLI_snprintf(msg,
                 sizeof(msg),
                 TIP_("Sample color for %s"),
                 !data->sample_palette ?
                     TIP_("Brush. Use Left Click to sample for palette instead") :
                     TIP_("Palette. Use Left Click to sample more colors"));
    ED_workspace_status_text(C, msg);
  }
}

static int sample_color_exec(bContext *C, wmOperator *op)
{
  Paint *paint = BKE_paint_get_active_from_context(C);
  Brush *brush = BKE_paint_brush(paint);
  ePaintMode mode = BKE_paintmode_get_active_from_context(C);
  ARegion *ar = CTX_wm_region(C);
  wmWindow *win = CTX_wm_window(C);
  const bool show_cursor = ((paint->flags & PAINT_SHOW_BRUSH) != 0);
  int location[2];
  paint->flags &= ~PAINT_SHOW_BRUSH;

  /* force redraw without cursor */
  WM_paint_cursor_tag_redraw(win, ar);
  WM_redraw_windows(C);

  RNA_int_get_array(op->ptr, "location", location);
  const bool use_palette = RNA_boolean_get(op->ptr, "palette");
  const bool use_sample_texture = (mode == PAINT_MODE_TEXTURE_3D) &&
                                  !RNA_boolean_get(op->ptr, "merged");

  paint_sample_color(C, ar, location[0], location[1], use_sample_texture, use_palette);

  if (show_cursor) {
    paint->flags |= PAINT_SHOW_BRUSH;
  }

  WM_event_add_notifier(C, NC_BRUSH | NA_EDITED, brush);

  return OPERATOR_FINISHED;
}

static int sample_color_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  Scene *scene = CTX_data_scene(C);
  Paint *paint = BKE_paint_get_active_from_context(C);
  Brush *brush = BKE_paint_brush(paint);
  SampleColorData *data = (SampleColorData *)MEM_mallocN(sizeof(SampleColorData),
                                                         "sample color custom data");
  ARegion *ar = CTX_wm_region(C);
  wmWindow *win = CTX_wm_window(C);

  data->event_type = event->type;
  data->show_cursor = ((paint->flags & PAINT_SHOW_BRUSH) != 0);
  copy_v3_v3(data->initcolor, BKE_brush_color_get(scene, brush));
  data->sample_palette = false;
  op->customdata = data;
  paint->flags &= ~PAINT_SHOW_BRUSH;

  sample_color_update_header(data, C);

  WM_event_add_modal_handler(C, op);

  /* force redraw without cursor */
  WM_paint_cursor_tag_redraw(win, ar);
  WM_redraw_windows(C);

  RNA_int_set_array(op->ptr, "location", event->mval);

  ePaintMode mode = BKE_paintmode_get_active_from_context(C);
  const bool use_sample_texture = (mode == PAINT_MODE_TEXTURE_3D) &&
                                  !RNA_boolean_get(op->ptr, "merged");

  paint_sample_color(C, ar, event->mval[0], event->mval[1], use_sample_texture, false);
  WM_cursor_modal_set(win, WM_CURSOR_EYEDROPPER);

  WM_event_add_notifier(C, NC_BRUSH | NA_EDITED, brush);

  return OPERATOR_RUNNING_MODAL;
}

static int sample_color_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  Scene *scene = CTX_data_scene(C);
  SampleColorData *data = (SampleColorData *)op->customdata;
  Paint *paint = BKE_paint_get_active_from_context(C);
  Brush *brush = BKE_paint_brush(paint);

  if ((event->type == data->event_type) && (event->val == KM_RELEASE)) {
    if (data->show_cursor) {
      paint->flags |= PAINT_SHOW_BRUSH;
    }

    if (data->sample_palette) {
      BKE_brush_color_set(scene, brush, data->initcolor);
      RNA_boolean_set(op->ptr, "palette", true);
    }
    WM_cursor_modal_restore(CTX_wm_window(C));
    MEM_freeN(data);
    ED_workspace_status_text(C, NULL);

    return OPERATOR_FINISHED;
  }

  ePaintMode mode = BKE_paintmode_get_active_from_context(C);
  const bool use_sample_texture = (mode == PAINT_MODE_TEXTURE_3D) &&
                                  !RNA_boolean_get(op->ptr, "merged");

  switch (event->type) {
    case MOUSEMOVE: {
      ARegion *ar = CTX_wm_region(C);
      RNA_int_set_array(op->ptr, "location", event->mval);
      paint_sample_color(C, ar, event->mval[0], event->mval[1], use_sample_texture, false);
      WM_event_add_notifier(C, NC_BRUSH | NA_EDITED, brush);
      break;
    }

    case LEFTMOUSE:
      if (event->val == KM_PRESS) {
        ARegion *ar = CTX_wm_region(C);
        RNA_int_set_array(op->ptr, "location", event->mval);
        paint_sample_color(C, ar, event->mval[0], event->mval[1], use_sample_texture, true);
        if (!data->sample_palette) {
          data->sample_palette = true;
          sample_color_update_header(data, C);
        }
        WM_event_add_notifier(C, NC_BRUSH | NA_EDITED, brush);
      }
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

static bool sample_color_poll(bContext *C)
{
  return (image_paint_poll_ignore_tool(C) || vertex_paint_poll_ignore_tool(C));
}

/******************** texture paint toggle operator ********************/

static bool texture_paint_toggle_poll(bContext *C)
{
  Object *ob = CTX_data_active_object(C);
  if (ob == NULL || ob->type != OB_MESH) {
    return 0;
  }
  if (!ob->data || ID_IS_LINKED(ob->data)) {
    return 0;
  }
  if (CTX_data_edit_object(C)) {
    return 0;
  }

  return 1;
}

static int texture_paint_toggle_exec(bContext *C, wmOperator *op)
{
  struct wmMsgBus *mbus = CTX_wm_message_bus(C);
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);
  const int mode_flag = OB_MODE_TEXTURE_PAINT;
  const bool is_mode_set = (ob->mode & mode_flag) != 0;

  if (!is_mode_set) {
    if (!ED_object_mode_compat_set(C, ob, (eObjectMode)mode_flag, op->reports)) {
      return OPERATOR_CANCELLED;
    }
  }

  if (ob->mode & mode_flag) {
    ob->mode &= ~mode_flag;

    if (U.glreslimit != 0) {
      GPU_free_images(bmain);
    }
    GPU_paint_set_mipmap(bmain, 1);

    toggle_paint_cursor(C, 0);
  }
  else {
    bScreen *sc;
    Image *ima = NULL;
    ImagePaintSettings *imapaint = &scene->toolsettings->imapaint;

    /* This has to stay here to regenerate the texture paint
     * cache in case we are loading a file */
    BKE_texpaint_slots_refresh_object(scene, ob);

    BKE_paint_proj_mesh_data_check(scene, ob, NULL, NULL, NULL, NULL);

    /* entering paint mode also sets image to editors */
    if (imapaint->mode == IMAGEPAINT_MODE_MATERIAL) {
      /* set the current material active paint slot on image editor */
      Material *ma = give_current_material(ob, ob->actcol);

      if (ma && ma->texpaintslot) {
        ima = ma->texpaintslot[ma->paint_active_slot].ima;
      }
    }
    else if (imapaint->mode == IMAGEPAINT_MODE_IMAGE) {
      ima = imapaint->canvas;
    }

    if (ima) {
      for (sc = (bScreen *)bmain->screens.first; sc; sc = (bScreen *)sc->id.next) {
        ScrArea *sa;
        for (sa = (ScrArea *)sc->areabase.first; sa; sa = sa->next) {
          SpaceLink *sl;
          for (sl = (SpaceLink *)sa->spacedata.first; sl; sl = sl->next) {
            if (sl->spacetype == SPACE_IMAGE) {
              SpaceImage *sima = (SpaceImage *)sl;

              if (!sima->pin) {
                Object *obedit = CTX_data_edit_object(C);
                ED_space_image_set(bmain, sima, obedit, ima, true);
              }
            }
          }
        }
      }
    }

    ob->mode |= mode_flag;

    BKE_paint_init(bmain, scene, PAINT_MODE_TEXTURE_3D, PAINT_CURSOR_TEXTURE_PAINT);

    BKE_paint_toolslots_brush_validate(bmain, &imapaint->paint);

    if (U.glreslimit != 0) {
      GPU_free_images(bmain);
    }
    GPU_paint_set_mipmap(bmain, 0);

    toggle_paint_cursor(C, 1);
  }

  Mesh *me = BKE_mesh_from_object(ob);
  BLI_assert(me != NULL);
  DEG_id_tag_update(&me->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_SCENE | ND_MODE, scene);

  // TODO max-k WM_msg_publish_rna_prop causes unresolved external symbol "struct PropertyRNA
  // rna_Object_mode"
  // WM_msg_publish_rna_prop(mbus, &ob->id, ob, Object, mode);

  WM_toolsystem_update_from_context_view3d(C);

  return OPERATOR_FINISHED;
}

static int brush_colors_flip_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  UnifiedPaintSettings *ups = &scene->toolsettings->unified_paint_settings;

  Paint *paint = BKE_paint_get_active_from_context(C);
  Brush *br = BKE_paint_brush(paint);

  if (ups->flag & UNIFIED_PAINT_COLOR) {
    swap_v3_v3(ups->rgb, ups->secondary_rgb);
  }
  else if (br) {
    swap_v3_v3(br->rgb, br->secondary_rgb);
  }
  else {
    return OPERATOR_CANCELLED;
  }

  WM_event_add_notifier(C, NC_BRUSH | NA_EDITED, br);

  return OPERATOR_FINISHED;
}

static bool brush_colors_flip_poll(bContext *C)
{
  if (image_paint_poll(C)) {
    Brush *br = image_paint_brush(C);
    if (ELEM(br->imagepaint_tool, PAINT_TOOL_DRAW, PAINT_TOOL_FILL)) {
      return true;
    }
  }
  else {
    Object *ob = CTX_data_active_object(C);
    if (ob != NULL) {
      if (ob->mode & (OB_MODE_VERTEX_PAINT | OB_MODE_TEXTURE_PAINT)) {
        return true;
      }
    }
  }
  return false;
}

// CUSTOM FUNCTIONS FOR WIDGET

void Widget_TexturePaint::update_brush(int new_brush)
{
  bContext *C = vr_get_obj()->ctx;
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    /* Exit edit mode */
    VR_UI::editmode_exit = true;
    Widget_Transform::transform_space = VR_UI::TRANSFORMSPACE_LOCAL;
    return;
  }

  // If not in TEXTURE_PAINT mode, toggle
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Object *ob = OBACT(view_layer);
  if (!(ob->mode & OB_MODE_TEXTURE_PAINT)) {
    texture_paint_toggle_exec(C, &paint_dummy_op);
  }

  // Sculpt *sd = CTX_data_tool_settings(C)->sculpt;
  // Brush *br = BKE_paint_brush(&sd->paint);
  Brush *br = image_paint_brush(C);
  br->imagepaint_tool = new_brush;
  brush = new_brush;
}

void Widget_TexturePaint::drag_start(VR_UI::Cursor &c)
{

  printf("drag started\n");

      if (c.bimanual)
  {
    return;
  }

  bContext *C = vr_get_obj()->ctx;
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    return;
  }

  /* Start paint tool operation */
  if (paint_dummy_op.type == 0) {
    paint_dummy_op.type = WM_operatortype_find("PAINT_OT_image_paint", true);
    if (paint_dummy_op.type == 0) {
      return;
    }
  }
  if (paint_dummy_op.ptr == 0) {
    paint_dummy_op.ptr = (PointerRNA *)MEM_callocN(sizeof(PointerRNA), __func__);
    if (paint_dummy_op.ptr == 0) {
      return;
    }
    WM_operator_properties_create_ptr(paint_dummy_op.ptr, paint_dummy_op.type);
    WM_operator_properties_sanitize(paint_dummy_op.ptr, 0);
  }
  if (paint_dummy_op.reports == 0) {
    paint_dummy_op.reports = (ReportList *)MEM_mallocN(sizeof(ReportList), "wmOperatorReportList");
    if (paint_dummy_op.reports == 0) {
      return;
    }
    BKE_reports_init(paint_dummy_op.reports, RPT_STORE | RPT_FREE);
  }

  cursor_side = c.side;

  // Toggle mode if not in TEXTURE_PAINT mode already
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Object *ob = OBACT(view_layer);
  if (!(ob->mode & OB_MODE_TEXTURE_PAINT)) {
    texture_paint_toggle_exec(C, &paint_dummy_op);
  }

  /* Scale parameters based on distance from hmd. */
  const Mat44f &hmd = VR_UI::hmd_position_get(VR_SPACE_REAL);
  p_hmd = *(Coord3Df *)hmd.m[3];
  p_cursor = *(Coord3Df *)c.position.get().m[3];
  dist = (p_cursor - p_hmd).length();

  paint_radius_prev = paint_radius;
  paint_strength_prev = paint_strength;

  /* Save original paint mode. */
  mode_orig = mode;

  if (VR_UI::shift_key_get()) {
    param_mode = true;
    stroke_canceled = false;
  }
  else {
    if (VR_UI::ctrl_key_get()) {
      if (mode_orig == BRUSH_STROKE_NORMAL) {
        mode = BRUSH_STROKE_INVERT;
      }
      else {
        mode = BRUSH_STROKE_NORMAL;
      }
    }

    if (CTX_data_active_object(C)) {
      stroke_started = true;
      /* Perform stroke */
      int invoke=paint_invoke(C, &paint_dummy_op, &paint_dummy_event);
      if (invoke == OPERATOR_CANCELLED) {
        stroke_canceled = true;
      }
      else {
        stroke_canceled = false;
      }

    }
  }

  // for (int i = 0; i < VR_SIDES; ++i) {
  //	Widget_TexturePaint::obj.do_render[i] = true;
  //}

  is_dragging = true;
}

void Widget_TexturePaint::drag_contd(VR_UI::Cursor &c)
{
  if (c.bimanual) {
    return;
  }

  if (stroke_canceled) {
    return;
  }

  bContext *C = vr_get_obj()->ctx;
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    return;
  }

  if (VR_UI::shift_key_get()) {
    param_mode = true;
    const Coord3Df &p = *(Coord3Df *)c.position.get().m[3];
    float current_dist = (p - p_hmd).length();
    float delta = (p - p_cursor).length()/BRUSH_SCALE;

    

    /* Adjust radius */
    if ((current_dist < dist)) {
      paint_radius = paint_radius_prev + delta;
      if (paint_radius > WIDGET_TEXTUREPAINT_MAX_RADIUS) {
        paint_radius = WIDGET_TEXTUREPAINT_MAX_RADIUS;
      }
    }
    else {
      paint_radius = paint_radius_prev - delta;
      if (paint_radius < 0.0f) {
        paint_radius = 0.0f;
      }
    }

    printf("scaling! %f + %f = %f\n", paint_radius_prev, delta, paint_radius);

  }else if (!param_mode) {
    bContext *C = vr_get_obj()->ctx;
    if (CTX_data_active_object(C)) {
      // paint_stroke_exec(C, &paint_dummy_op);
      paint_exec(C, &paint_dummy_op);
    }
  }

  // for (int i = 0; i < VR_SIDES; ++i) {
  //	Widget_TexturePaint::obj.do_render[i] = true;
  //}

  is_dragging = true;
}

void Widget_TexturePaint::drag_stop(VR_UI::Cursor &c)
{
  printf("drag stop\n");
  if (c.bimanual) {
    return;
  }

  if (stroke_canceled) {
    return;
  }

  is_dragging = false;

  bContext *C = vr_get_obj()->ctx;
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    /* Exit edit mode */
    VR_UI::editmode_exit = true;
    Widget_Transform::transform_space = VR_UI::TRANSFORMSPACE_LOCAL;
    return;
  }

  if (VR_UI::shift_key_get()) {
    param_mode = true;
    const Coord3Df &p = *(Coord3Df *)c.position.get().m[3];
    float current_dist = (p - p_hmd).length();
    float delta = (p - p_cursor).length();

    /* Adjust radius */
    if ((current_dist < dist)) {
      paint_radius = paint_radius_prev + delta;
      if (paint_radius > WIDGET_TEXTUREPAINT_MAX_RADIUS) {
        paint_radius = WIDGET_TEXTUREPAINT_MAX_RADIUS;
      }
    }
    else {
      paint_radius = paint_radius_prev - delta;
      if (paint_radius < 0.0f) {
        paint_radius = 0.0f;
      }
    }
  }

  if (stroke_started) {
    bContext *C = vr_get_obj()->ctx;
    if (CTX_data_active_object(C)) {
      paint_stroke_done(C, (PaintStroke*) paint_dummy_op.customdata);
    }
  }

  /* Restore original paint mode. */
  mode = mode_orig;

  stroke_started = false;
  param_mode = false;

  // for (int i = 0; i < VR_SIDES; ++i) {
  //	Widget_TexturePaint::obj.do_render[i] = false;
  //}
}

static void render_gimbal(const float radius,
                          const float color[4],
                          const bool filled,
                          const float arc_partial_angle,
                          const float arc_inner_factor)
{
  /* Adapted from dial_geom_draw() in dial3d_gizmo.c */

  GPU_line_width(1.0f);
  GPUVertFormat *format = immVertexFormat();
  uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

  immUniformColor4fv(color);

  if (filled) {
    imm_draw_circle_fill_2d(pos, 0, 0, radius, 100.0f);
  }
  else {
    if (arc_partial_angle == 0.0f) {
      imm_draw_circle_wire_2d(pos, 0, 0, radius, 100.0f);
      if (arc_inner_factor != 0.0f) {
        imm_draw_circle_wire_2d(pos, 0, 0, arc_inner_factor, 100.0f);
      }
    }
    else {
      float arc_partial_deg = RAD2DEGF((M_PI * 2) - arc_partial_angle);
      imm_draw_circle_partial_wire_2d(pos, 0, 0, radius, 100.0f, 0.0f, arc_partial_deg);
    }
  }

  immUnbindProgram();
}

void Widget_TexturePaint::render(VR_Side side)
{
  // if (param_mode) {
  //	/* Render measurement text. */
  //	const Mat44f& prior_model_matrix = VR_Draw::get_model_matrix();
  //	static Mat44f m;
  //	m = VR_UI::hmd_position_get(VR_SPACE_REAL);
  //	const Mat44f& c = VR_UI::cursor_position_get(VR_SPACE_REAL, cursor_side);
  //	memcpy(m.m[3], c.m[3], sizeof(float) * 3);
  //	VR_Draw::update_modelview_matrix(&m, 0);

  //	VR_Draw::set_depth_test(false, false);
  //	VR_Draw::set_color(0.8f, 0.8f, 0.8f, 1.0f);
  //	static std::string param_str;
  //	sprintf((char*)param_str.data(), "%.3f", sculpt_radius);
  //	VR_Draw::render_string(param_str.c_str(), 0.02f, 0.02f, VR_HALIGN_CENTER, VR_VALIGN_TOP,
  // 0.0f, 0.08f, 0.001f);

  //	VR_Draw::set_depth_test(true, true);
  //	VR_Draw::update_modelview_matrix(&prior_model_matrix, 0);
  //}

  static float color[4] = {1.0f, 1.0f, 1.0f, 0.8f};
  switch ((eBrushSculptTool)brush) {
    case PAINT_TOOL_DRAW:
    case PAINT_TOOL_SOFTEN:
    case PAINT_TOOL_SMEAR:
    case PAINT_TOOL_CLONE:
    case PAINT_TOOL_FILL:
    case PAINT_TOOL_MASK: {
      if (Widget_TexturePaint::is_dragging) {
        if (Widget_TexturePaint::mode == BRUSH_STROKE_INVERT) {
          color[0] = 0.0f;
          color[1] = 0.0f;
          color[2] = 1.0f;
        }
        else {
          color[0] = 1.0f;
          color[1] = 0.0f;
          color[2] = 0.0f;
        }
      }
      else {
        if (VR_UI::ctrl_key_get()) {
          if (Widget_TexturePaint::mode_orig == BRUSH_STROKE_INVERT) {
            color[0] = 1.0f;
            color[1] = 0.0f;
            color[2] = 0.0f;
          }
          else {
            color[0] = 0.0f;
            color[1] = 0.0f;
            color[2] = 1.0f;
          }
        }
        else {
          if (Widget_TexturePaint::mode_orig == BRUSH_STROKE_INVERT) {
            color[0] = 0.0f;
            color[1] = 0.0f;
            color[2] = 1.0f;
          }
          else {
            color[0] = 1.0f;
            color[1] = 0.0f;
            color[2] = 0.0f;
          }
        }
      }
      break;
    }
    default: {
      color[0] = 1.0f;
      color[1] = 1.0f;
      color[2] = 1.0f;
      break;
    }
  }

  if (raycast) {
    /* Render paint circle. */
    GPU_blend(true);
    GPU_matrix_push();
    static Mat44f m = VR_Math::identity_f;
    m = vr_get_obj()->t_eye[VR_SPACE_BLENDER][side];
    memcpy(
        m.m[3], VR_UI::cursor_position_get(VR_SPACE_BLENDER, cursor_side).m[3], sizeof(float) * 3);
    GPU_matrix_mul(m.m);
    GPU_polygon_smooth(false);
    //GPU_matrix_translate_2f(Widget_TexturePaint::mouse[0], Widget_TexturePaint::mouse[1]);
    

    color[3] = 0.8f;
    render_gimbal(BRUSH_SCALE*paint_radius, color, false, 0.0f, 0.0f);

    GPU_blend(false);
    GPU_matrix_pop();
  }
  else {
    /* Render paint ball. */
    const Mat44f &prior_model_matrix = VR_Draw::get_model_matrix();

    VR_Draw::update_modelview_matrix(&VR_UI::cursor_position_get(VR_SPACE_REAL, cursor_side), 0);
    // VR_Draw::set_depth_test(false, false);
    // color[3] = 0.1f;
    // VR_Draw::set_color(color);
    // VR_Draw::render_ball(paint_radius);
    // VR_Draw::set_depth_test(true, false);
    color[3] = 0.1f;
    VR_Draw::set_color(color);
    VR_Draw::render_ball(paint_radius);
    // VR_Draw::set_depth_test(true, true);

    VR_Draw::update_modelview_matrix(&prior_model_matrix, 0);
  }

  // Widget_TexturePaint::obj.do_render[side] = false;
}
