/* $Id: ArgSlider.cpp,v 1.5 2004/08/16 09:34:48 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include <gtkmm.h>

#include "think.h"

#include "ArgSlider.h"

ArgSlider::ArgSlider (thArg *arg)
	: Gtk::HScale (arg->argMin, arg->argMax, .01)
{
	arg_ = arg;
	set_value (arg_->argValues[0]);
	set_draw_value(true);
	set_value_pos(Gtk::POS_TOP  );
	set_digits(10);
}

ArgSlider::~ArgSlider ()
{
}

void ArgSlider::on_value_changed ()
{
	Gtk::HScale::on_value_changed();
	arg_->argValues[0] = get_value();
}
