/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include <stdio.h>
#include <stdlib.h>

#include <gtkmm.h>

#include "think.h"

#include "ArgTable.h"

ArgTable::ArgTable (void)
	: Gtk::Table(1, 3), rows_(1), args_(0)
{
	
}

ArgTable::~ArgTable (void)
{
}

void ArgTable::insertArg (thArg *arg)
{
	if (arg == NULL)
		return;

	int row = args_++;

	Gtk::Label *label = manage(new Gtk::Label((arg->label().length() > 0) ?
											  arg->label() : arg->name()));
	Gtk::HScale *slider = manage(new Gtk::HScale(arg->min(),arg->max(),.0001));

	Gtk::Adjustment *argAdjust = slider->get_adjustment();

	slider->set_draw_value(false);

	slider->signal_value_changed().connect(
		sigc::bind<Gtk::HScale *, thArg *>(
			sigc::mem_fun(*this, &ArgTable::sliderChanged),
			slider, arg));

	arg->signal_arg_changed().connect(
		sigc::bind<Gtk::HScale *>(
			sigc::mem_fun(*this, &ArgTable::argChanged),
			slider));

	slider->set_value((*arg)[0]);
	
	Gtk::SpinButton *valEntry = manage(new Gtk::SpinButton(
										   *argAdjust, .0001,
										   4));

	if (args_ > rows_)
	{
		resize(args_, 3);
		rows_ = args_;
	}

	attach(*label, 0, 1, row, row+1, Gtk::SHRINK,
		   Gtk::SHRINK);
	attach(*slider, 1, 2, row, row+1,
		   Gtk::EXPAND|Gtk::FILL,
		   Gtk::EXPAND|Gtk::FILL);
	attach(*valEntry, 2, 3, row, row+1,
		   Gtk::SHRINK|Gtk::FILL,
		   Gtk::SHRINK|Gtk::FILL);
}
	
void ArgTable::sliderChanged (Gtk::HScale *slider, thArg *arg)
{
	arg->setValue(slider->get_value());
}

void ArgTable::argChanged (thArg *arg, Gtk::HScale *slider)
{
	slider->set_value((*arg)[0]);
}
