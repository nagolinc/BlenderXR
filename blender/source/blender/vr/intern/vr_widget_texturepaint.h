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

/** \file blender/vr/intern/vr_widget_texturepaint.h
*   \ingroup vr
*/

#ifndef __VR_WIDGET_TEXTUREPAINT_H__
#define __VR_WIDGET_TEXTUREPAINT_H__

#include "vr_widget.h"

/* Interaction widget for the TexturePaint tool. */
class Widget_TexturePaint : public VR_Widget
{
	friend class Widget_SwitchLayout;
	friend class Widget_SwitchComponent;
	friend class Widget_Menu;
  friend class Widget_Select;
  friend class Widget_Transform;

public:
	static float paint_radius;	/* paint stroke radius. */
	static float paint_strength;	/* paint stroke strength. */
protected:
	static Coord3Df p_hmd;	/* HMD reference point for adjusting paint radius / strength. */
	static Coord3Df p_cursor;	/* Cursor reference point for adjusting paint radius / strength. */
	static float dist;	/* The reference distance between p_hmd and p_cursor. */
	static float paint_radius_prev;	/* The previous paint radius. */
	static float paint_strength_prev;	/* The previous paint strength. */
	static bool param_mode;	/* Whether the paint tool was in adjust parameters mode. */

	static bool stroke_started;	/* Whether a paint stroke was started on drag_start(). */
	static bool is_dragging; /* Whether the paint tool is currently dragging. */
  static bool stroke_canceled;    /* Whether the paint tool is currently canceled. */
 public:
	static VR_Side cursor_side;	/* Side of the current interaction cursor. */
public:
	static int mode;	/* The current paint mode (add or subtract). */
	static int mode_orig;	/* The original paint mode on drag_start(). */
	static int brush;	/* The current paint brush. */
	static float location[3];	/* The 3D location of the paint cursor. */
	static float mouse[2];	/* The 2D-projected location of the paint cursor. */
	static float pressure;	/* The paint trigger pressure. */
	static bool use_trigger_pressure;	/* Whether to use trigger pressure (or paint strength). */
	static bool raycast;	/* Whether the paint tool is in raycast (or proximity) mode. */
	static bool dyntopo;	/* Whether dyntopo is enabled. */
	static char	symmetry;	/* The current symmetry state. */
	static bool pen_flip;	/* Whether the paint widget is in pen flip mode. */
	static bool ignore_background_click;	/* Whether to ignore background clicks. */

	
	static void update_brush(int new_brush);	/* Update the current paint brush.*/

	static Widget_TexturePaint obj;	/* Singleton implementation object. */
	virtual std::string name() override { return "TEXTUREPAINT"; };	/* Get the name of this widget. */
	virtual Type type() override { return TYPE_TEXTUREPAINT; };	/* Type of Widget. */

	virtual void drag_start(VR_UI::Cursor& c) override;	/* Start a drag/hold-motion with the index finger / trigger. */
	virtual void drag_contd(VR_UI::Cursor& c) override;	/* Continue drag/hold with index finger / trigger. */
	virtual void drag_stop(VR_UI::Cursor& c) override;	/* Stop drag/hold with index finger / trigger. */

	virtual void render(VR_Side side) override;	/* Apply the widget's custom render function (if any). */
};

#endif /* __VR_WIDGET_TEXTUREPAINT_H__ */
